"""Summarize actual exports and package only firmware-analysis artifacts."""
import csv
import json
import re
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'decompiled'
rows = []
package_files = []
function_contents = {}
for name in ('nuttx_ap.bin', 'nuttx_apc1.bin', 'nuttx_hifi.bin', 'nuttx_user.bin'):
    folder = OUT / 'exports' / name
    summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8'))
    index = list(csv.DictReader((folder / 'functions.tsv').open(encoding='utf-8'), delimiter='\t'))
    combined=(folder/'all_functions.c').read_text(encoding='utf-8')
    headers=list(re.finditer(r'(?m)^/\* ([0-9a-fA-F]{8}) \| [^\n]+ \| body bytes=\d+ \*/$',combined))
    by_address={m.group(1):combined[m.start():headers[n+1].start() if n+1<len(headers) else len(combined)]
                for n,m in enumerate(headers)}
    flags = Counter()
    flagged = []
    for entry in index:
        path = folder / entry['file']
        text = by_address[entry['address']]
        reasons = []
        if 'halt_baddata' in text or 'Bad instruction' in text:
            reasons.append('unsupported_or_bad_instruction')
        if 'WARNING' in text:
            reasons.append('decompiler_warning')
        if entry['status'] != 'decompiled':
            reasons.append('failed')
        if reasons:
            flagged.append({'address': entry['address'], 'name': entry['name'], 'reasons': reasons})
        flags.update(reasons)
        function_contents[path.relative_to(ROOT).as_posix()]=text
    summary['bad_instruction_functions'] = flags['unsupported_or_bad_instruction']
    summary['warning_functions'] = flags['decompiler_warning']
    summary['note'] = 'Counts concern recognized function candidates. A C output does not prove correctness or completeness.'
    (folder / 'quality.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    (folder / 'review_needed.json').write_text(json.dumps(flagged, ensure_ascii=False, indent=2), encoding='utf-8')
    rows.append(summary)
    print('Quality complete:',name,'functions:',len(index),flush=True)
    package_files.extend(p for p in folder.iterdir() if p.is_file())

lines = ['# A1 V1.6.88 全镜像静态反编译结果', '',
    '本次对 OTA 包里的四个程序镜像都进行了自动分析并导出。**这不是完整原始源码，也不是可以重新编译、直接烧录的工程。**', '',
    'ARM 镜像恢复为 Thumb 汇编和 C 风格伪代码；HiFi4 镜像只恢复当前 Xtensa 解码器支持的部分。程序中未识别的指令、缺失的 ROM/运行时内容和动态重定位都会造成缺口。', '',
    '## 实际输出统计', '',
    '| 镜像 | 识别函数候选 | 输出伪代码 | 无伪代码 | 含坏指令/中断标记 | 含 WARNING |',
    '| --- | ---: | ---: | ---: | ---: | ---: |']
for row in rows:
    lines.append(f"| {row['image']} | {row['functions']} | {row['decompiled']} | {row['failed']} | {row['bad_instruction_functions']} | {row['warning_functions']} |")
lines += ['', '这些是工具统计，不能解释为源码恢复率；函数候选可能误识别，多个警告分类可能重叠。没有警告也不等于已人工验证正确。', '',
    '## 怎么看', '',
    '- 每个 `exports/<镜像>/all_functions.c`：该镜像全部已识别函数的合并伪代码。',
    '- `functions/`：按地址拆分的单函数伪代码；`functions.tsv` 是本轮权威清单。',
    '- `disassembly.asm`：已识别函数的反汇编，不是将每个数据字节强行解释为指令。',
    '- `calls.tsv`：已解析的静态函数调用关系；间接调用不一定可恢复。',
    '- `inferred_names.tsv`：从诊断字符串和交叉引用推测出的名称，非调试符号。',
    '- `quality.json`、`review_needed.json`、`failures.tsv`：质量和失败记录。',
    '- `memory.tsv`、`bookmarks.tsv`、`topic_xrefs.tsv`：内存映射、分析问题、重点字符串引用（如已导出）。',
    '- `REVIEW.md`：人工抽查的关键函数和后续研究入口。', '',
    '## 映射和限制', '',
    '1. AP 裸镜像映射到 `0x10190000`，APC1 映射到 `0x10990000`；基址来自镜像头、启动指令和内部指针。非缓存别名没有重复映射，避免同一份代码被重复统计。',
    '2. AP 恢复启动代码明确复制的初始化数据：`0x1053abc0 → 0x201d16f0`，长度 `0xb684`。其他动态数据和芯片内 ROM 不在此 OTA 包内。',
    '3. APC1 本轮 SRAM 仅建立未初始化地址空间；启动复制记录为 `0x10c2ce44 → 0x200e7c68`，结束 `0x200eb210`，还可进一步补全。',
    '4. HIFI 按镜像头中的复制表还原到 `0x0088xxxx` 代码区和 `0x208cxxxx` 数据区。HiFi4 专有扩展/FLIX 指令不能由通用 Xtensa 解码器完整处理。',
    '5. USER 很可能为 HiFi4 动态算法库；本轮使用文件相对地址，尚未应用专有动态重定位，所以伪代码只适合作线索。',
    '6. 自动分析无法恢复已经丢失的原始注释、宏、准确类型/变量名、构建配置和所有源文件结构，也无法补出本次包未包含的 Boot ROM/完整工厂分区。', '',
    '## 工具及本次遇到的问题', '',
    '- 使用官方 Ghidra 12.1.3、Microsoft JDK 21 和 Capstone；只在电脑上做静态分析，没有执行固件、升级设备或改变设备密钥。',
    '- 中文安装路径使 Java XML/JAR 资源路径解析失败，因此实际运行目录改为英文临时目录，结果复制回此目录。',
    '- Ghidra Java 源脚本编译后未能激活 OSGi bundle；改用 `javac` 预编译和 Headless 支持的 `.class` 脚本入口。',
    '- 裸镜像的加载块地址不等于 Ghidra 的 imageBase 元数据；修正为使用实际块地址，避免错误重定位。本交付采用修正后的 v4 工程输出。', '',
    '## 复现', '',
    '在固件实验目录中用 `.venv\\Scripts\\python.exe prepare-arm-inputs.py` 生成候选，再分别执行 `run-headless.py ap/apc1/hifi/user`（每次选一个参数）。已有工程重导出用 `--process`，只导出分析清单用 `--inventory`。',
    '运行器使用 `%LOCALAPPDATA%/Temp/a1-ghidra-20260907/` 的英文工作目录，需要先把官方 Ghidra 解压至该目录；JDK 在实验目录的 `tools/jdk21/`。工具没有放进结果压缩包。', '',
    '## 参考', '',
    '- [Ghidra 官方发布](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.3_build)',
    '- [Ghidra HiFi4 指令支持讨论](https://github.com/NationalSecurityAgency/ghidra/issues/6342)', '']
(OUT / 'README.md').write_text('\n'.join(lines), encoding='utf-8')
(OUT / 'summary.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
package_files += [OUT / 'README.md', OUT / 'summary.json']
if (OUT / 'REVIEW.md').exists():
    package_files.append(OUT / 'REVIEW.md')
package_files += list((ROOT / 'ghidra_scripts').glob('*.java'))
package_files += list((OUT / 'focus').glob('*.c'))
package_files += [ROOT / n for n in ('prepare-arm-inputs.py', 'layout-probe.py', 'run-headless.py', 'build-decomp-report.py', 'annotate-focus.py')]
target = ROOT / 'A1-V1.6.88-static-decompilation.zip'
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for name,content in function_contents.items():
        archive.writestr(name,content)
    for path in sorted(set(package_files)):
        archive.write(path, path.relative_to(ROOT).as_posix())
print(json.dumps(rows, ensure_ascii=False, indent=2))
print('Package:', target, 'bytes:', target.stat().st_size)
