import ui

class WarpShowerWindow(ui.ScriptWindow):
	def __init__(self):
		# The stream owns this top layer window across GameWindow.Close().
		ui.ScriptWindow.__init__(self, "CURTAIN")
		self.loadingAniImage = None
		self.escapeEvent = None

	def __del__(self):
		ui.ScriptWindow.__del__(self)

	def SetEscapeEvent(self, event):
		self.escapeEvent = event

	def Open(self):
		if self.loadingAniImage is None:
			loader = ui.PythonScriptLoader()
			loader.LoadScriptFile(self, "uiscript/warpshowerwindow.py")
			self.loadingAniImage = self.GetChild("LoadingAniImage")
		self.SetTop()
		self.Show()

	def Close(self):
		self.Hide()

	def Destroy(self):
		self.Close()
		self.escapeEvent = None
		self.loadingAniImage = None
		self.ClearDictionary()

	def OnPressEscapeKey(self):
		if self.escapeEvent:
			self.escapeEvent()
		return True
