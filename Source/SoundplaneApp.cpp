	
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneApp.h"

SoundplaneApp::SoundplaneApp() :
	mpModel(0),
	mpView(0),
	mpController(0),
	mpBorder(0),
	mpWindow(0)
{
}

SoundplaneApp::~SoundplaneApp()
{
	
	delete mpController;
	delete mpModel;
	delete mpView;
	delete mpBorder;
	delete mpWindow;
}

void SoundplaneApp::initialise (const String& commandLine)
{
	mpModel = new SoundplaneModel();

	mpController = new SoundplaneController(mpModel);
	mpView = new SoundplaneView(mpModel, mpController, mpController);
	
	mpBorder = new MLAppBorder(mpView);
	mpBorder->makeResizer(mpView);
	mpBorder->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);
	mpBorder->setBounds(mpView->getBounds());
	
	// add view to window but retain ownership here
	bool resizeToFit = true;
	
	mpWindow = new MLAppWindow();	
	mpWindow->setContentNonOwned(mpBorder, resizeToFit);
	mpWindow->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);
	mpWindow->setUsingOpenGL(true);
	mpWindow->setVisible(true);
	

	mpWindow->setConstrainer (mpBorder->getConstrainer());
	

	
	
	// setBounds(mpView->getBounds());  // MLTEST NX
	
	mpBorder->addAndMakeVisible(mpView);
	mpController->setView(mpView);

	mpController->initialize();	
	mpView->initialize();		// MLTEST NX to remove


	// generate a persistent state for the Model
	mpModelState = std::unique_ptr<MLAppState>(new MLAppState(mpModel, "", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));

	// generate a persistent state for the application's view. 
	mpViewState = std::unique_ptr<MLAppState>(new MLAppState(mpView, "View", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));

	MLConsole() << "Starting Soundplane...\n";	
	// separate thread needed for JUCE-based app
	mpModel->startProcessThread();
	

	if (!mpModelState->loadStateFromAppStateFile()) 
	{
		setDefaultWindowSize();
		mpController->doWelcomeTasks(); 
	}
	mpController->fetchAllProperties();
	mpView->goToPage(0);

	ModifierKeys k = ModifierKeys::getCurrentModifiersRealtime();
	if((k.isCommandDown()) || (!mpViewState->loadStateFromAppStateFile()))
	{
		setDefaultWindowSize();
	}
	else
	{
		mpView->updateAllProperties();	
	}
}
	
void SoundplaneApp::shutdown()
{
	mpModelState->updateAllProperties();
	mpModelState->saveStateToStateFile();
	
	mpViewState->updateAllProperties();
	mpViewState->saveStateToStateFile();

	if(mpController) mpController->setView(0);
	
}

void SoundplaneApp::setDefaultWindowSize()
{
	if(mpWindow)
	{
		mpWindow->centreWithSize(800, 800*kSoundplaneViewGridUnitsY/kSoundplaneViewGridUnitsX);
	}
}

bool SoundplaneApp::moreThanOneInstanceAllowed()
{
	return false;
}
