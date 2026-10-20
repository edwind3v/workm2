from __future__ import print_function

import ast
import ctypes
import os
import re
import sys
import types
import __builtin__

SOURCE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.environ.get('METIN2_PACK_ROOT', os.path.join(SOURCE, 'client_pack'))
ROOT = os.path.join(PACK, 'root')
checks = 0


def passed(label):
    global checks
    checks += 1
    print('PASS: ' + label)


class Widget(object):
    def __init__(self):
        self.closed = 0
    def __getattr__(self, name):
        if name in ('GetWidth', 'GetHeight'):
            return lambda: 640
        return lambda *args: None
    def Close(self):
        self.closed += 1
    def __del__(self):
        pass


class Net(object):
    PHASE_WINDOW_LOAD = 4
    def __init__(self):
        self.selected = []
        self.windows = {}
    def SendSelectCharacterPacket(self, slot):
        self.selected.append(slot)
    def SetPhaseWindow(self, phase, window):
        self.windows[phase] = window
    def ClearPhaseWindow(self, phase, window):
        if self.windows.get(phase) is window:
            del self.windows[phase]


def module(**values):
    result = types.ModuleType('stub')
    result.__dict__.update(values)
    return result


app = module(ENABLE_LOADING_PERFORMANCE=True, GetRandom=lambda a, b: a,
             SetFrameSkip=lambda value: None)
net = Net()
env = dict(app=app, net=net,
           ui=module(ScriptWindow=Widget, ExpandedImageBox=Widget),
           wndMgr=module(GetScreenWidth=lambda: 1024, GetScreenHeight=lambda: 768),
           uiScriptLocale=module(LOCALE_UISCRIPT_PATH='locale/common/ui/'),
           dbg=module(TraceError=lambda value: None))

for name in ('introloading.py', 'networkmodule.py', 'playersettingmodule.py',
             'uiguild.py', 'game.py', 'interfacemodule.py', 'uiminimap.py', 'uiwarpshower.py'):
    data = open(os.path.join(ROOT, name), 'rb').read()
    compile(data, name, 'exec')
passed('edited root scripts compile under Python 2.7')

tree = ast.parse(open(os.path.join(ROOT, 'introloading.py'), 'rb').read())
native_class = None
for node in tree.body:
    if isinstance(node, ast.If):
        for child in node.body:
            if isinstance(child, ast.ClassDef) and child.name == 'NewLoadingWindow':
                native_class = child
assert native_class is not None
exec compile(ast.fix_missing_locations(ast.Module(body=[native_class])), 'native-window', 'exec') in env
NewLoadingWindow = env['NewLoadingWindow']

tree = ast.parse(open(os.path.join(ROOT, 'networkmodule.py'), 'rb').read())
stream_class = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == 'MainStream')
methods = [node for node in stream_class.body if isinstance(node, ast.FunctionDef)
           and node.name in ('Close', 'SetWarpPhase', 'SetLoadingPhase',
                             'OpenWarpShowerWindow', 'CloseWarpShowerWindow')]
assert len(methods) == 5
test_class = ast.ClassDef(name='TestStream', bases=[ast.Name(id='object', ctx=ast.Load())],
                         body=methods, decorator_list=[])
exec compile(ast.fix_missing_locations(ast.Module(body=[test_class])), 'native-phase', 'exec') in env
TestStream = env['TestStream']
TestStream.GetCharacterSlot = lambda self: 3
TestStream.SetPhaseWindow = lambda self, window: setattr(self, 'chosen', window)
TestStream.SetLoginPhase = lambda self: setattr(self, 'login_requested', True)

class LegacyWindow(object):
    def __init__(self, stream):
        raise AssertionError('Old Python asset-loading window entered in optimized mode')

sys.modules['introLoading'] = module(NewLoadingWindow=NewLoadingWindow, LoadingWindow=LegacyWindow)
stream = TestStream()
stream.SetLoadingPhase()
window = stream.chosen
assert isinstance(window, NewLoadingWindow)
assert not hasattr(window.__class__, 'LoadData')
assert 'loadStepList' not in window.__dict__
window.Open()
assert net.selected == [3]
window.Close()
assert not net.windows
passed('optimized loading uses the native window, sends one select packet, and has no Python asset queue')

net.selected = []
current, pending = Widget(), Widget()
stream.curPhaseWindow = current
stream.newPhaseWindow = pending
stream.popupWindow = Widget()
stream.curtain = Widget()
stream.warpLoadingWindow = Widget()
stream.warpTransitionActive = False
stream.warpTargetOpened = False
stream.SetWarpPhase()
assert current.closed == 1 and pending.closed == 1
assert stream.curPhaseWindow is stream and stream.newPhaseWindow == 0
assert net.selected == [3]
stream.Close()
assert stream.curPhaseWindow == 0
passed('warp closes old phases once and completes the select handshake without recursive Close')

tree = ast.parse(open(os.path.join(ROOT, 'playersettingmodule.py'), 'rb').read())
names = set(('__InitData', '__LoadGameSound', '__LoadGameEffect', '__LoadGameWarrior',
             '__LoadGameAssassin', '__LoadGameSura', '__LoadGameShaman', '__LoadGameWolfman'))
defs = [node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef) and node.name in names]
assert len(defs) == len(names)
scope = dict(app=app)
exec compile(ast.fix_missing_locations(ast.Module(body=defs)), 'native-asset-guards', 'exec') in scope
for name in names:
    scope[name]()
passed('all Python animation, effect, sound, and character asset loaders return before engine calls')

source = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
native = (open(os.path.join(source, 'UserInterface', 'PythonLoading.cpp'), 'rb').read() +
          open(os.path.join(source, 'UserInterface', 'PythonLoading.hpp'), 'rb').read()).lower()
asset_groups = names | set(('__LoadGameWarriorEx', '__LoadGameAssassinEx', '__LoadGameSuraEx',
                           '__LoadGameShamanEx', '__LoadGameWolfmanEx', 'SetGeneralMotions',
                           'SetNewGeneralMotions', 'SetIntroMotions'))
asset_count = 0
for node in ast.walk(tree):
    if isinstance(node, ast.FunctionDef) and node.name in asset_groups:
        for value in ast.walk(node):
            if isinstance(value, ast.Str) and value.s.lower().endswith(('.mse', '.msa', '.msf', '.wav', '.msm')):
                asset_count += 1
                assert value.s.lower() in native, (node.name, value.s)
assert asset_count == 561
passed('all 561 literal asset references in the migrated Python groups exist in native source/tables')

layout = os.path.join(os.path.dirname(ROOT), 'uiscript', 'uiscript', 'warpshowerwindow.py')
layout_scope = dict(uiScriptLocale=env['uiScriptLocale'], SCREEN_WIDTH=1024, SCREEN_HEIGHT=768)
sys.modules['uiScriptLocale'] = env['uiScriptLocale']
exec compile(open(layout, 'rb').read(), layout, 'exec') in layout_scope
images = layout_scope['window']['children'][0]['children'][0]['children'][0]['images']
assert len(images) == 59
for image in images:
    assert os.path.exists(os.path.join(os.path.dirname(ROOT), 'locale', image.replace('/', os.sep)))
passed('warp overlay layout references all 59 installed animation frames')

original_import = __builtin__.__import__
probe = module(TOOLTIP_WARRIOR='Warrior')
def hooked_import(name, globals=None, locals=None, fromlist=None, level=-1):
    if name.lower() == 'owsaplocaleprobe':
        return probe
    return original_import(name, globals, locals, fromlist, level)
__builtin__.__import__ = hooked_import
try:
    native_import = ctypes.pythonapi.PyImport_ImportModule
    native_import.argtypes = [ctypes.c_char_p]
    native_import.restype = ctypes.py_object
    imported = native_import('owsapLocaleProbe')
    assert imported is probe and imported.TOOLTIP_WARRIOR == 'Warrior'
finally:
    __builtin__.__import__ = original_import
passed('native locale import uses the custom Python import hook and retains loaded attributes')

print('%d integration checks passed; engine rendering and server login require a live client run.' % checks)
