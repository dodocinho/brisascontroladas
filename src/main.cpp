#include "ofMain.h"
#include "ofApp.h"
#include "GuiApp.h"
#include "ofAppGLFWWindow.h"
#include "WindowManager.h"


//========================================================================
int main(){
	
	int xGUI, yGUI, wGUI, hGUI;
	int xDisplay, yDisplay, wDisplay, hDisplay;
	
	// Posição inicial e tamanho da tela de controle
	xGUI = 0;
	yGUI = 20;
	wGUI = 1333;
	hGUI = 950;

	// Posição inicial e tamanho da tela de exibição
	xDisplay = 0;
	yDisplay = 0;
	wDisplay = 1024;
	hDisplay = 768;

	ofGLFWWindowSettings settings;

	settings.setPosition(ofVec2f( xDisplay, yDisplay ));
	settings.setSize(wDisplay, hDisplay);
	settings.resizable = true;
	settings.decorated = true;
	settings.multiMonitorFullScreen = true;
	settings.numSamples = 8;
	shared_ptr<ofAppBaseWindow> mainWindow = ofCreateWindow(settings);

	settings.setPosition(ofVec2f( xGUI,yGUI));
	settings.setSize(wGUI, hGUI);
	settings.resizable = true;
	settings.decorated = true;
	settings.multiMonitorFullScreen = false;
	settings.numSamples = 8 ;
	settings.shareContextWith = mainWindow;
	shared_ptr<ofAppBaseWindow> guiWindow = ofCreateWindow(settings);

	auto mainApp = std::make_shared<ofApp>();
	auto guiApp = std::make_shared<GuiApp>();

	mainApp->gui = guiApp;
	mainApp->window = mainWindow;
	mainApp->gl = dynamic_pointer_cast<ofBaseGLRenderer>(mainWindow->renderer());

	ofRunApp(guiWindow, guiApp);
	ofRunApp(mainWindow, mainApp);
	ofRunMainLoop();

}
