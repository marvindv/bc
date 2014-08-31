// main.cpp
// Include the Irrlicht header
#include "irrlicht.h"

#include "GUIMain.hpp"
#include "SimulationModel.hpp"
#include "ScenarioChoice.hpp"
#include "MyEventReceiver.hpp"
#include "Network.hpp"
#include "IniFile.hpp"

#include <cstdlib> //For rand(), srand()
#include <vector>
#include <sstream>

// Irrlicht Namespaces
using namespace irr;

int main()
{

    //Read basic ini settings
    std::string iniFilename = "bc4.ini";
    u32 graphicsWidth = IniFile::iniFileTou32(iniFilename, "graphics_width");
    u32 graphicsHeight = IniFile::iniFileTou32(iniFilename, "graphics_height");
    u32 graphicsDepth = IniFile::iniFileTou32(iniFilename, "graphics_depth");
    bool fullScreen = (IniFile::iniFileTou32(iniFilename, "graphics_mode")==1); //1 for full screen

    //create device
    IrrlichtDevice* device = createDevice(video::EDT_OPENGL, core::dimension2d<u32>(graphicsWidth,graphicsHeight),graphicsDepth,fullScreen,false,false,0);
    device->setWindowCaption(L"Bridge Command 5.0 Alpha");

    video::IVideoDriver* driver = device->getVideoDriver();
    scene::ISceneManager* smgr = device->getSceneManager();

    //Choose scenario
    ScenarioChoice scenarioChoice(device);
    std::string scenarioName = scenarioChoice.chooseScenario();

    //seed random number generator
    std::srand(device->getTimer()->getTime());

    //create GUI
    GUIMain guiMain(device);

    //Create simulation model
    SimulationModel model (device, smgr, &guiMain, scenarioName);

    //create event receiver, linked to model
    MyEventReceiver receiver(&model, &guiMain);
    device->setEventReceiver(&receiver);

    //Create networking, linked to model
    Network network(&model);
    network.connectToServer();

    //main loop
    while(device->run())
    {

        network.update();
        model.update();

        //Render
        driver->beginScene(true,true,video::SColor(255,100,101,140));
        smgr->drawAll();
        guiMain.drawGUI();
        driver->endScene();

    }

    device->drop();
    //networking should be stopped (presumably with destructor when it goes out of scope?)


    return(0);
}
