"""Launch portable Ghidra directly, avoiding Windows batch encoding issues."""
import argparse
import subprocess
import sys
import os
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parent
WORK=Path(os.environ['LOCALAPPDATA'])/'Temp/a1-ghidra-20260907'
GHIDRA=WORK/'ghidra_12.1.3_PUBLIC'
JAVA=ROOT/'tools/jdk21/jdk-21.0.12.1+1/bin/java.exe'
IMAGES={
    'ap':('nuttx_ap.bin','ARM:LE:32:v8-m','0x10190000','PrepareArm.java'),
    'apc1':('nuttx_apc1.bin','ARM:LE:32:v8-m','0x10990000','PrepareArm.java'),
    'hifi':('nuttx_hifi.bin','Xtensa:LE:32:default','0x10050000','PrepareXtensa.java'),
    'user':('nuttx_user.bin','Xtensa:LE:32:default','0x00000000','PrepareXtensa.java'),
}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('image',choices=IMAGES)
    parser.add_argument('--process',action='store_true',help='Re-export an existing project without rerunning preparation')
    parser.add_argument('--inventory',action='store_true',help='Export memory, bookmarks, and topic references from existing project')
    parser.add_argument('--string-xrefs',action='store_true',help='Export all string-to-function references from an existing project')
    parser.add_argument('--apply-names',action='store_true',help='Apply reviewed semantic names to an existing project')
    parser.add_argument('--apply-rejections',action='store_true',help='Remove reviewed false function candidates')
    args=parser.parse_args()
    name,lang,base,prep=IMAGES[args.image]
    logname=args.image+('-apply-rejections' if args.apply_rejections else '-apply-names' if args.apply_names else '-string-xrefs' if args.string_xrefs else '-inventory' if args.inventory else '-reexport' if args.process else '')
    dirs=WORK/'decompiled'
    shutil.copytree(ROOT/'ghidra_scripts',WORK/'ghidra_scripts',dirs_exist_ok=True)
    shutil.copytree(ROOT/'decompiled/inputs',dirs/'inputs',dirs_exist_ok=True)
    shutil.copytree(ROOT/'unpacked-V1.6.88',WORK/'firmware',dirs_exist_ok=True)
    classes=WORK/'compiled-scripts'
    classes.mkdir(exist_ok=True)
    libs=os.pathsep.join(str(p.relative_to(GHIDRA))+'/*' for p in GHIDRA.rglob('lib') if p.is_dir())
    subprocess.run([str(JAVA.with_name('javac.exe')),'-encoding','UTF-8','-cp',libs,'-d',str(classes),
                    *map(str,(WORK/'ghidra_scripts').glob('*.java'))],cwd=GHIDRA,check=True,
                    creationflags=subprocess.CREATE_NO_WINDOW)
    for d in ('ghidra-projects','logs','exports'):(dirs/d).mkdir(parents=True,exist_ok=True)
    cmd=[str(JAVA),'-Xmx3G','-Xshare:off','-XX:ParallelGCThreads=2','-XX:CICompilerCount=2',
         '-Djava.system.class.loader=ghidra.GhidraClassLoader','-Dfile.encoding=UTF-8',
         '-Duser.language=en','-Duser.country=US','-Djava.awt.headless=true',
         '-Dapplication.settingsdir='+str(WORK/'settings'/args.image),
         '-Dapplication.cachedir='+str(WORK/'cache'/args.image),
         '-cp',str(GHIDRA/'Ghidra/Framework/Utility/lib/Utility.jar'),
         'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',
         str(dirs/'ghidra-projects'),'A1_'+args.image.upper()+'_v4']
    if args.process or args.inventory or args.string_xrefs or args.apply_names or args.apply_rejections:
        cmd+=['-process',name,'-noanalysis']
    else:
        cmd+=['-import',str(WORK/'firmware'/name),'-processor',lang,'-cspec','default',
              '-loader','BinaryLoader','-loader-baseAddr',base]
    cmd+=['-scriptPath',str(classes)]
    if not (args.process or args.inventory or args.string_xrefs or args.apply_names or args.apply_rejections):
        cmd+=['-preScript',prep.replace('.java','.class'),str(dirs/'inputs')]
    post_script=('RemoveRejectedFunctions.class' if args.apply_rejections else
                 'ApplySemanticNames.class' if args.apply_names else
                 'ExportStringXrefs.class' if args.string_xrefs else
                 'ExportInventory.class' if args.inventory else 'ExportPseudo.class')
    post_arg=(str(ROOT/'analysis/rejected_function_candidates.tsv') if args.apply_rejections else
              str(ROOT/'analysis/semantic_names.tsv') if args.apply_names else str(dirs/'exports'))
    cmd+=['-postScript',post_script,post_arg,'-analysisTimeoutPerFile','1800',
          '-max-cpu','4','-log',str(dirs/'logs'/(logname+'.log')),
          '-scriptlog',str(dirs/'logs'/(logname+'-script.log'))]
    logfile=dirs/'logs'/(logname+'-console.log')
    print('Starting',name,'log:',logfile,flush=True)
    with logfile.open('w',encoding='utf-8') as log:
        result=subprocess.run(cmd,cwd=WORK,stdout=log,stderr=subprocess.STDOUT,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    logtext=logfile.read_text(encoding='utf-8',errors='replace')
    # Headless can exit 0 even after a pre/post-script exception, as observed here.
    exitcode=result.returncode or (1 if 'REPORT SCRIPT ERROR' in logtext else 0)
    print('Ghidra exit code:',result.returncode,'effective result:',exitcode,flush=True)
    if not args.apply_names and (dirs/'exports'/name).exists():
        shutil.copytree(dirs/'exports'/name,ROOT/'decompiled/exports'/name,dirs_exist_ok=True)
    for path in (dirs/'logs').glob(logname+'*'):
        shutil.copy2(path,ROOT/'decompiled/logs'/path.name)
    if exitcode==0:
        project='A1_'+args.image.upper()+'_v4'
        local_projects=ROOT/'decompiled/ghidra-projects'
        local_projects.mkdir(exist_ok=True)
        shutil.copy2(dirs/'ghidra-projects'/(project+'.gpr'),local_projects/(project+'.gpr'))
        shutil.copytree(dirs/'ghidra-projects'/(project+'.rep'),local_projects/(project+'.rep'),dirs_exist_ok=True)
    if exitcode:
        print(logtext[-7000:])
    return exitcode

if __name__=='__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
