import ui
import app
import wndMgr

class WarpShowerWindow(ui.ScriptWindow):
	def __init__(self):
		# This stream-owned window survives old GameWindow/interface teardown.
		ui.ScriptWindow.__init__(self, "CURTAIN")
		self.escapeEvent = None
		self.imageNames = app.GetLoadingScreenImages()
		self.images = []
		self.imageIndex = 0
		self.screenSize = None
		if not self.imageNames:
			raise RuntimeError("No cached loading-screen backgrounds are available")
		for filename in self.imageNames:
			image = ui.ExpandedImageBox()
			image.SetParent(self)
			image.LoadImage(filename)
			image.SetPosition(0, 0)
			image.Hide()
			self.images.append(image)
		self.__Resize()
		self.images[self.imageIndex].Show()
		self.Hide()

	def __del__(self):
		ui.ScriptWindow.__del__(self)

	def SetEscapeEvent(self, event):
		self.escapeEvent = event

	def SelectBackground(self, chooseNew=False):
		if chooseNew:
			self.images[self.imageIndex].Hide()
			self.imageIndex = app.GetRandom(0, len(self.images) - 1)
			self.images[self.imageIndex].Show()
		return self.imageNames[self.imageIndex]

	def __Resize(self):
		size = (wndMgr.GetScreenWidth(), wndMgr.GetScreenHeight())
		if size == self.screenSize:
			return
		self.screenSize = size
		self.SetSize(*size)
		for image in self.images:
			image.SetScale(float(size[0]) / max(1, image.GetWidth()),
				float(size[1]) / max(1, image.GetHeight()))

	def Open(self):
		self.__Resize()
		self.SetTop()
		self.Show()

	def Close(self):
		self.Hide()

	def Destroy(self):
		self.Close()
		self.escapeEvent = None
		self.images = []
		self.imageNames = ()
		self.ClearDictionary()

	def OnPressEscapeKey(self):
		if self.escapeEvent:
			self.escapeEvent()
		return True
