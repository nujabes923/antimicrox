"""Package the deployed Windows build without personal data or old binaries."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import zipfile
from datetime import datetime

root = Path(__file__).resolve().parents[1]
installed = root / 'dist/AntiMicroX-RandomTurbo'
name = 'AntiMicroX-易用版-' + datetime.now().strftime('%Y%m%d-%H%M%S')
stage = root / 'dist' / name
app = stage / 'App'
app.mkdir(parents=True)
for item in installed.iterdir():
    if item.is_dir():
        shutil.copytree(item, app / item.name)
    elif item.suffix.lower() == '.dll' or item.name in ('antimicrox.exe', 'ActionRecorder.exe'):
        shutil.copy2(item, app / item.name)

def write(path, text):
    path.write_text(text.replace('\n', '\r\n'), encoding='utf-8-sig')

# Avoid locale-dependent Chinese paths in batch commands: use an ASCII subdirectory.
for filename, exe in [('双击启动.cmd', 'antimicrox.exe'), ('单独打开录制工具.cmd', 'ActionRecorder.exe')]:
    (stage / filename).write_bytes(('@echo off\r\ncd /d "%~dp0App"\r\nstart "" "%~dp0App\\' + exe + '"\r\n').encode('ascii'))

translations = stage / 'share/antimicrox/translations'
translations.mkdir(parents=True)
subprocess.run(['C:/msys64/mingw64/bin/lrelease.exe', str(root / 'share/antimicrox/translations/antimicrox_zh_CN.ts'), '-qm', str(translations / 'antimicrox_zh_CN.qm')], check=True, capture_output=True)

write(stage / '先看这里.txt', '''AntiMicroX 易用版（Windows 10 / 11，64 位）

【第一次使用，只需三步】
1. 右键收到的 ZIP 压缩包 → 全部提取（解压）。
   建议放到桌面或 D 盘；不要直接在压缩包里双击运行。
2. 打开解压后的文件夹，双击“ 双击启动.cmd ”。
   如果电脑隐藏了扩展名，看到的名字就是“双击启动”。
3. 根据用途，按下面的方法操作。无需安装 Python 或开发工具。

【功能一：手柄映射键盘】
先连接手柄，再启动程序。按一下手柄，找到界面里亮起的对应按钮，
点击它并设置要模拟的键盘按键。设置后点 Save / 保存，保存自己的配置。
下次可用 Load / 加载打开。朋友的手柄型号不同，需要自行设置按键。

【功能二：定时补药 / auto buff（无需手柄）】
主界面底部点 auto buff，选择药水，设置游戏中对应的单个按键，
点“开始（3 秒后）”，在倒计时内切回游戏。
可以打开“小浮窗”查看状态或停止。关闭用药窗口 / 浮窗会停止。
切到其他窗口时暂停发送；程序只按时间按键，不判断是否成功用药。

【功能三：录制键盘和鼠标（无需手柄）】
双击“单独打开录制工具”，或在主程序的 Action Library 中点“快速录制”。
F8 开始录制，3 秒内切到目标窗口；再按 F8 结束录制。
先“试跑一次”确认，再设置循环次数。F9 按设置播放，F10 停止。
录制期间的点击和按键会真实执行。播放时保持原游戏和窗口大小。

【如何停止 / 退出】
录制工具：F10 停止。
动作库试跑、定时用药：Ctrl + Alt + F10 停止。
彻底退出主程序：在右下角托盘找到 AntiMicroX 图标，右键退出。
点窗口右上角 X 可能只是缩到托盘；日常不用时请彻底退出。

【常见问题】
• 找不到手柄：确认 Windows 已识别手柄，重新连接后再打开程序。
• 游戏内没反应：先在记事本测试映射，并检查游戏按键设置。
  若游戏以管理员身份运行，可右键 App 文件夹内 antimicrox.exe，
  选择“以管理员身份运行”，再测试。
• 提示缺少 DLL：重新完整解压，不要只复制 exe，也不要随意删 App 的文件。
• 中文界面：随包附简体中文翻译；中文系统通常自动使用。
  新增功能的部分按钮仍是英文，可对照上面的中英文名称。
• 录制工具无法启动：它使用 Windows 自带的 .NET Framework 4.x。
  如出现相关错误，把错误截图发给分享者协助处理。

【保存与移动】
这是免安装包，设置和动作库仍保存在当前 Windows 用户的数据目录，
并不是所有数据都保存在这个文件夹里。
程序设置：%LOCALAPPDATA%\\antimicrox
动作库：%LOCALAPPDATA%\\AntiMicroX-RandomTurbo\\actions
手柄配置：保存在你点“保存”时选择的位置。
如果搬家或换电脑，需要另外备份这些内容。
已保存的录制动作若引用旧安装路径，搬家后需在动作步骤中更新程序路径。
本包不包含分享者的个人配置、账号信息或录制记录。

【文件夹不要拆散】
双击启动：主程序入口。
单独打开录制工具：录制入口。
App、share：程序和语言资源，请保留。
说明与开源：详细功能说明、许可、源码；普通使用无需打开。

这是基于 AntiMicroX 的自定义版本，不是官方原版安装包。
''')
info = stage / '说明与开源'
info.mkdir()
shutil.copy2(root / 'LICENSE', info / 'LICENSE.txt')
shutil.copy2(root / 'ACTION_LIBRARY.md', info / '详细功能说明.txt')
licenses = info / '运行库许可'
license_root = Path('C:/msys64/mingw64/share/licenses')
if license_root.exists():
    shutil.copytree(license_root, licenses)
tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
allowed_dirs = {'src', 'cmake', 'share', 'other', 'tools', 'tests', '.github'}
with zipfile.ZipFile(info / '项目源码.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for rel in tracked:
        p = Path(rel)
        if rel and (p.parts[0] in allowed_dirs or len(p.parts) == 1) and p.name != 'AGENTS.md' and (root / p).is_file():
            archive.write(root / p, 'AntiMicroX-source/' + p.as_posix())
    if 'tools/package-share.py' not in tracked:
        archive.write(__file__, 'AntiMicroX-source/tools/package-share.py')

# Resolve native imports against the package and Windows, never the developer PATH.
objdump = 'C:/msys64/mingw64/bin/objdump.exe'
native_files = list(app.rglob('*.dll')) + [app / 'antimicrox.exe', app / 'ActionRecorder.exe']
missing = set()
for binary in native_files:
    dump = subprocess.check_output([objdump, '-p', binary.name], cwd=binary.parent, text=True, errors='replace')
    for dep in re.findall(r'DLL Name:\s*(\S+)', dump):
        if dep.lower().startswith(('api-ms-', 'ext-ms-')):
            continue
        if not any((base / dep).exists() for base in [app, binary.parent, Path(os.environ['WINDIR']) / 'System32']):
            missing.add(dep)
if missing:
    raise RuntimeError('Missing runtime files: ' + ', '.join(sorted(missing)))
env = os.environ.copy()
env['PATH'] = str(Path(os.environ['WINDIR']) / 'System32') + ';' + os.environ['WINDIR']
for key in list(env):
    if key.startswith('QT_'):
        del env[key]
for exe, arg in [('antimicrox.exe', '--version'), ('ActionRecorder.exe', '--self-test')]:
    result = subprocess.run([str(app / exe), arg], cwd=app, env=env, capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f'{exe} verification failed: {result.returncode}: {result.stderr!r}')
    print(exe, 'verification passed', result.stdout.decode(errors='replace').strip())
hashes = {p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in stage.rglob('*') if p.is_file()}
(info / '文件校验.json').write_text(json.dumps(hashes, ensure_ascii=False, indent=2), encoding='utf-8')
output = root / 'dist' / (name + '.zip')
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for p in stage.rglob('*'):
        if p.is_file():
            archive.write(p, name + '/' + p.relative_to(stage).as_posix())
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for rel, expected in hashes.items():
        assert hashlib.sha256(archive.read(name + '/' + rel)).hexdigest() == expected
assert hashlib.sha256((app / 'antimicrox.exe').read_bytes()).digest() == hashlib.sha256((installed / 'antimicrox.exe').read_bytes()).digest()
print(json.dumps({'zip': str(output), 'size_mb': round(output.stat().st_size / 1024**2, 1), 'native_files_checked': len(native_files), 'files': len(hashes)}, ensure_ascii=False))
