"""Browse USB files AFTER explicitly enabling ADB. Does not enable it itself."""
import sys
from dingtalk_a1 import Identity
from dingtalk_a1.usb import USBStorage

identity = Identity.load(sys.argv[1])
storage = USBStorage(identity.serial_number, adb=sys.argv[2] if len(sys.argv) > 2 else "adb")
storage.start_server()
print(storage.space())
print(storage.list("/emmc/audio"))
# Explicit writes, only if desired:
# storage.mkdir('/emmc/mindlink')      # create once
# storage.upload('notes.txt', '/emmc/mindlink/notes.txt')
# storage.download('/emmc/mindlink/notes.txt', 'returned-notes.txt')
# storage.delete_file('/emmc/mindlink/notes.txt')
