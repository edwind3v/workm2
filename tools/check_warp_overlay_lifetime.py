from __future__ import print_function
import ast
import copy
import os
import sys
import types

SOURCE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = os.environ.get('METIN2_PACK_ROOT', os.path.join(SOURCE, 'client_pack'))
ROOT = os.path.join(PACK, 'root')
events = []
state = dict(network=False, map=False, character=False)


def module(**values):
    result = types.ModuleType('stub')
    result.__dict__.update(values)
    return result


class Window(object):
    def __init__(self, layer='UI'):
        self.layer = layer
        self.visible = False
        self.destroyed = False
    def Show(self):
        self.visible = True
    def Hide(self):
        self.visible = False
    def SetTop(self):
        pass
    def GetChild(self, name):
        return object()
    def ClearDictionary(self):
        pass
    def __del__(self):
        pass


class Loader(object):
    def LoadScriptFile(self, window, path):
        assert path == 'uiscript/warpshowerwindow.py'


class Curtain(object):
    def __init__(self):
        self.fades = 0
        self.pending = None
    def FadeIn(self):
        self.fades += 1
    def FadeOut(self, callback):
        self.fades += 1
        self.pending = callback
    def Cancel(self):
        self.pending = None


app = module(ENABLE_LOADING_PERFORMANCE=True, SetFrameSkip=lambda n: events.append(('skip', n)),
             Exit=lambda: events.append(('exit',)))
ui = module(ScriptWindow=Window, PythonScriptLoader=Loader, __mem_func__=lambda fn: fn,
            WOC_ENABLE_PRINT_REGISTERS=False)
sys.modules['ui'] = ui
net = module(SendSelectCharacterPacket=lambda slot: events.append(('select', slot)),
             Disconnect=lambda: events.append(('disconnect',)))
loading = module(IsWorldReady=lambda: all(state.values()))
sys.modules['loading'] = loading

overlay_scope = dict(ui=ui)
exec compile(open(os.path.join(ROOT, 'uiwarpshower.py'), 'rb').read(), 'uiwarpshower', 'exec') in overlay_scope
Overlay = overlay_scope['WarpShowerWindow']
sys.modules['uiWarpShower'] = module(WarpShowerWindow=Overlay)

tree = ast.parse(open(os.path.join(ROOT, 'networkmodule.py'), 'rb').read())
original_class = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'MainStream')
names = set(('Close', 'SetPhaseWindow', '__ChangePhaseWindow', 'SetWarpPhase', 'SetGamePhase',
             'OpenWarpShowerWindow', 'CloseWarpShowerWindow', 'OnWarpWorldRendered',
             'SetLoginPhase', 'Destroy'))
methods = [copy.deepcopy(n) for n in original_class.body if isinstance(n, ast.FunctionDef) and n.name in names]
assert len(methods) == len(names)
for method in methods:
    method.decorator_list = []
test_class = ast.ClassDef(name='Stream', bases=[ast.Name(id='object', ctx=ast.Load())],
                         body=methods, decorator_list=[])
scope = dict(ui=ui, app=app, net=net, constInfo=module(ENABLE_UI_DEBUG_WINDOW=False))
exec compile(ast.fix_missing_locations(ast.Module(body=[test_class])), 'stream-warp-state', 'exec') in scope
Stream = scope['Stream']
Stream.GetCharacterSlot = lambda self: 2


class Popup(object):
    def Close(self):
        pass
    def Destroy(self):
        pass


stream = Stream()
stream.warpLoadingWindow = None
stream.warpTransitionActive = False
stream.warpTargetOpened = False
stream.newPhaseWindow = 0
stream.curtain = Curtain()
stream.popupWindow = Popup()


class Game(object):
    def __init__(self, owner):
        self.owner = owner
        self.closed = 0
    def Open(self):
        # Existing GameWindow.Open re-enables frame skipping.
        app.SetFrameSkip(1)
        events.append(('game_open',))
    def Close(self):
        assert self.owner.warpLoadingWindow.visible
        self.closed += 1
        state['map'] = False
        events.append(('game_close',))


sys.modules['game'] = module(GameWindow=Game)
stream.curPhaseWindow = Game(stream)

for attempt in range(2):
    state.update(network=True, map=True, character=True)
    old_game = stream.curPhaseWindow
    stream.OpenWarpShowerWindow()
    overlay = stream.warpLoadingWindow
    assert overlay.layer == 'CURTAIN' and overlay.visible
    stream.OnWarpWorldRendered()
    assert overlay.visible, 'Old world must not release the new warp overlay'
    fade_count = stream.curtain.fades
    stream.SetWarpPhase()
    assert old_game.closed == 1 and stream.curPhaseWindow is stream
    assert stream.warpLoadingWindow is overlay and overlay.visible
    assert stream.curtain.fades == fade_count and stream.curtain.pending is None
    assert events.count(('select', 2)) == attempt + 1

    stream.SetGamePhase()
    assert isinstance(stream.curPhaseWindow, Game)
    assert stream.warpTargetOpened and overlay.visible
    assert stream.curtain.fades == fade_count, 'No gray/black phase fades during warp'
    assert events[-1] == ('skip', 0)
    for ready in (dict(network=False, map=True, character=True),
                  dict(network=True, map=False, character=True),
                  dict(network=True, map=True, character=False)):
        state.update(ready)
        stream.OnWarpWorldRendered()
        assert overlay.visible
    state.update(network=True, map=True, character=True)
    assert overlay.visible, 'Readiness alone must not hide a window before render notification'
    stream.OnWarpWorldRendered()
    assert not overlay.visible and not stream.warpTransitionActive
    assert not stream.warpTargetOpened and events[-1] == ('skip', 1)
print('PASS: two teleports retain one overlay through interface destruction and reveal only after a ready destination render')


class Login(object):
    def __init__(self, owner):
        pass
    def Open(self):
        pass


sys.modules['introLogin'] = module(LoginWindow=Login)
stream.OpenWarpShowerWindow()
stream.warpLoadingWindow.OnPressEscapeKey()
assert not stream.warpLoadingWindow.visible and not stream.warpTransitionActive
assert ('disconnect',) in events
assert stream.curtain.pending is not None, 'Ordinary login retains its normal phase fade'
print('PASS: escape clears the persistent overlay and normal login fades remain enabled')

stream.OpenWarpShowerWindow()
stream.curPhaseWindow = 0
stream.newPhaseWindow = 0
stream.Destroy()
assert stream.warpLoadingWindow is None and not stream.warpTransitionActive
print('PASS: stream shutdown releases the persistent overlay')

game_tree = ast.parse(open(os.path.join(ROOT, 'game.py'), 'rb').read())
game_class = next(n for n in game_tree.body if isinstance(n, ast.ClassDef) and n.name == 'GameWindow')
render = next(n for n in game_class.body if isinstance(n, ast.FunctionDef) and n.name == 'OnRender')
render_calls = [n for n in ast.walk(render) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)]
world_draw = next(n for n in render_calls if n.func.attr == 'RenderGame')
notify = next(n for n in render_calls if n.func.attr == 'OnWarpWorldRendered')
assert notify.lineno > world_draw.lineno
interface_data = open(os.path.join(ROOT, 'interfacemodule.py'), 'rb').read()
assert 'wndWarpShower' not in interface_data
layout = os.path.join(os.path.dirname(ROOT), 'uiscript', 'uiscript', 'warpshowerwindow.py')
assert '"color": 0xFF000000' in open(layout, 'rb').read()
print('PASS: render notification follows world drawing; the interface has no overlay ownership and the backdrop is opaque')
