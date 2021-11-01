/*   Bridge Command 5.0 Ship Simulator
     Copyright (C) 2014 James Packer

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY Or FITNESS For A PARTICULAR PURPOSE.  See the
     GNU General Public License For more details.

     You should have received a copy of the GNU General Public License along
     with this program; if not, write to the Free Software Foundation, Inc.,
     51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "SimulationModel.hpp"

#include "ScenarioDataStructure.hpp"
#include "GUIMain.hpp"
#include "Terrain.hpp"
#include "Sky.hpp"
#include "Buoys.hpp"
#include "Sound.hpp"

#include "IniFile.hpp"
#include "Constants.hpp"
#include "Utilities.hpp"

#include <cmath>
#include <fstream>

#ifdef WITH_PROFILING
#include "iprof.hpp"
#else
#define IPROF(a) //intentionally empty placeholder
#endif

//#include <ctime>

//using namespace irr;

SimulationModel::SimulationModel(irr::IrrlichtDevice* dev, irr::scene::ISceneManager* scene, GUIMain* gui, Sound* sound, ScenarioData scenarioData, OperatingMode::Mode mode, irr::f32 viewAngle, irr::f32 lookAngle, irr::f32 cameraMinDistance, irr::f32 cameraMaxDistance, irr::u32 disableShaders, irr::u32 waterSegments):
    manOverboard(irr::core::vector3df(0,0,0),scene,dev,this,&terrain) //Initialise MOB
    {
        //get reference to scene manager
        device = dev;
        smgr = scene;
        driver = scene->getVideoDriver();
        guiMain = gui;
		this->sound = sound;
        isMouseDown = false;

        //Store a serialised form of the scenario loaded, as we may want to send this over the network
        serialisedScenarioData = scenarioData.serialise();

        scenarioName = scenarioData.scenarioName;

        //store what mode we're in
        this->mode = mode;

        //store default view angle
        this->viewAngle = viewAngle;

        //Set loop number to zero
        loopNumber = 0;

        worldName = scenarioData.worldName;
        irr::f32 startTime = scenarioData.startTime;
        irr::u32 startDay=scenarioData.startDay;
        irr::u32 startMonth=scenarioData.startMonth;
        irr::u32 startYear=scenarioData.startYear;

        //load the sun times
        irr::f32 sunRise = scenarioData.sunRise;
        irr::f32 sunSet  = scenarioData.sunSet;
        if(sunRise==0.0) {sunRise=6;}
        if(sunSet==0.0) {sunSet=18;}

        //load the weather:
        //Fixme: add in wind direction etc
        weather = scenarioData.weather;
        rainIntensity = scenarioData.rainIntensity;
        visibilityRange = scenarioData.visibilityRange;
        if (visibilityRange <= 0) {visibilityRange = 5*M_IN_NM;} //TODO: Check units

        //Fixme: Think about time zone handling
        //Fixme: Note that if the time_t isn't long enough, 2038 problem exists
        scenarioOffsetTime = Utilities::dmyToTimestamp(startDay,startMonth,startYear);//Time in seconds to start of scenario day (unix timestamp for 0000h on day scenario starts)

        //set internal scenario time to start
        scenarioTime = startTime * SECONDS_IN_HOUR;

        //Set initial tide height to zero
        tideHeight = 0;

        if (worldName == "") {
            //Could not load world name from scenario, so end here
            std::cerr << "World model name not defined" << std::endl;
            exit(EXIT_FAILURE);
        }

        //construct path to world model
        std::string worldPath = "World/";
        worldPath.append(worldName);

        //Check if this world model exists in the user dir.
        std::string userFolder = Utilities::getUserDir();
        if (Utilities::pathExists(userFolder + worldPath)) {
            worldPath = userFolder + worldPath;
        }

        //Add terrain: Needs to happen first, so the terrain parameters are available
        terrain.load(worldPath, smgr);

        //sky box/dome
        Sky sky (smgr);

        //Load own ship model.
        ownShip.load(scenarioData.ownShipData, smgr, this, &terrain, device);
        if(mode == OperatingMode::Secondary) {
            ownShip.setSpeed(0); //Don't start moving if in secondary mode
        }

        //add water
        water.load(smgr,ownShip.getSceneNode(),weather,disableShaders,waterSegments);

        /* To be replaced by getting information and passing into gui load method.
        //Tell gui to hide the second engine scroll bar if we have a single engine
        if (ownShip.isSingleEngine()) {
            gui->setSingleEngine();
        }

        //Tell gui to hide all ship controls if in secondary mode
        if (mode == OperatingMode::Secondary) {
            gui->hideEngineAndRudder();
//      TODO      gui->hideWheel();
        }

        //Tell the GUI what instruments to display - currently GPS and depth sounder
        gui->setInstruments(ownShip.hasDepthSounder(),ownShip.getMaxSounderDepth(),ownShip.hasGPS());
        */

        //Load the radar with config parameters
        radarCalculation.load(ownShip.getRadarConfigFile(),device);

        //set camera zoom to 1
        zoom = 1.0;

        //make a camera, setting parent and offset
        std::vector<irr::core::vector3df> views = ownShip.getCameraViews(); //Get the initial camera offset from the own ship model
        irr::f32 angleCorrection = ownShip.getAngleCorrection();
        camera.load(smgr,device->getLogger(),ownShip.getSceneNode(),views,irr::core::degToRad(viewAngle),lookAngle,angleCorrection);
        camera.setNearValue(cameraMinDistance);
        camera.setFarValue(cameraMaxDistance);

        //make ambient light
        light.load(smgr,sunRise,sunSet, camera.getSceneNode());


        //Load other ships
        otherShips.load(scenarioData.otherShipsData,scenarioTime,mode,smgr,this,device);

        //Load buoys
        buoys.load(worldPath, smgr, this,device);

        //Load land objects
        landObjects.load(worldPath, smgr, this, terrain, device);

        //Load land lights
        landLights.load(worldPath, smgr, this, terrain);

        //Load tidal information
        tide.load(worldPath);

        //Load rain
        rain.load(smgr, camera.getSceneNode(), device);

        //make a radar screen, setting parent and offset from own ship
        radarScreen.load(smgr,ownShip.getSceneNode(), ownShip.getScreenDisplayPosition(), ownShip.getScreenDisplaySize(), ownShip.getScreenDisplayTilt());

        //make radar image - one for the background render, and one with any 2d drawing on top
        //Make as big as the maximum screen display size (next power of 2), and then only use as much as is needed to get 1:1 image to screen pixel mapping
        irr::u32 radarTextureSize = driver->getScreenSize().Height*0.4; // Optimised for the small radar screen (Where 0.6*screen height is used for the 3d view). We should have a higher resolution for full radar view
        irr::u32 largeRadarTextureSize = driver->getScreenSize().Height; // Optimised for the large radar screen
        //Find next power of 2 size
        radarTextureSize = std::pow(2,std::ceil(std::log2(radarTextureSize)));
        largeRadarTextureSize = std::pow(2,std::ceil(std::log2(largeRadarTextureSize)));

        //In simulationModel, keep track of the used size, and pass this to gui etc.
        radarImage = driver->createImage (irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(radarTextureSize, radarTextureSize)); //Create image for radar calculation to work on
        radarImageOverlaid = driver->createImage (irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(radarTextureSize, radarTextureSize)); //Create image for radar calculation to work on
        radarImageLarge = driver->createImage (irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(largeRadarTextureSize, largeRadarTextureSize)); //Create image for radar calculation to work on
        radarImageOverlaidLarge = driver->createImage (irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(largeRadarTextureSize, largeRadarTextureSize)); //Create image for radar calculation to work on
        //fill with bg colour
        radarImage->fill(irr::video::SColor(255, 128, 128, 128)); //Fill with background colour
        radarImageOverlaid->fill(irr::video::SColor(255, 128, 128, 128)); //Fill with background colour
        radarImageLarge->fill(irr::video::SColor(255, 128, 128, 128)); //Fill with background colour
        radarImageOverlaidLarge->fill(irr::video::SColor(255, 128, 128, 128)); //Fill with background colour

        //make radar camera
        std::vector<irr::core::vector3df> radarViews; //Get the initial camera offset from the radar screen
		irr::f32 screenTilt = ownShip.getScreenDisplayTilt();
        radarViews.push_back(ownShip.getScreenDisplayPosition() + irr::core::vector3df(0,0.5*sin(irr::core::DEGTORAD*screenTilt)*ownShip.getScreenDisplaySize(),-0.5*cos(irr::core::DEGTORAD*screenTilt)*ownShip.getScreenDisplaySize()));
        radarCamera.load(smgr, device->getLogger(),ownShip.getSceneNode(),radarViews,irr::core::PI/2.0,0,0);
		radarCamera.setLookUp(-1.0 * screenTilt); //FIXME: Why doesn't simply -1.0*screenTilt work?
		radarCamera.updateViewport(1.0);
        radarCamera.setNearValue(0.8*0.5*ownShip.getScreenDisplaySize());
        radarCamera.setFarValue(1.2*0.5*ownShip.getScreenDisplaySize());

        //Hide the man overboard model
        manOverboard.setVisible(false);

        //initialise offset
        offsetPosition = irr::core::vector3d<int64_t>(0,0,0);

        //store time
        previousTime = device->getTimer()->getTime();

        guiData = new GUIData;

    } //end of SimulationModel constructor

SimulationModel::~SimulationModel()
{
    radarImage->drop(); //We created this with 'create', so drop it when we're finished
    radarImageOverlaid->drop(); //We created this with 'create', so drop it when we're finished
    radarImageLarge->drop(); //We created this with 'create', so drop it when we're finished
    radarImageOverlaidLarge->drop(); //We created this with 'create', so drop it when we're finished

    delete guiData;
}

    irr::f32 SimulationModel::longToX(irr::f32 longitude) const
    {
        return terrain.longToX(longitude); //Cascade to terrain
    }

    irr::f32 SimulationModel::latToZ(irr::f32 latitude) const
    {
        return terrain.latToZ(latitude); //Cascade to terrain
    }

    void SimulationModel::setSpeed(irr::f32 spd)
    {
         ownShip.setSpeed(spd);
    }

    irr::f32 SimulationModel::getSpeed() const
    {
        return(ownShip.getSpeed());
    }

    irr::f32 SimulationModel::getLat()  const{
        return terrain.zToLat(ownShip.getPosition().Z + offsetPosition.Z);
    }

    irr::f32 SimulationModel::getLong() const{
        return terrain.xToLong(ownShip.getPosition().X + offsetPosition.X);
    }

    irr::f32 SimulationModel::getPosX() const{
        return ownShip.getPosition().X + offsetPosition.X;
    }

    irr::f32 SimulationModel::getPosZ() const{
        return ownShip.getPosition().Z + offsetPosition.Z;
    }

    irr::f32 SimulationModel::getCOG() const{
        return ownShip.getCOG();
    }

    irr::f32 SimulationModel::getSOG() const{
        return ownShip.getSOG();
    }

    irr::f32 SimulationModel::getDepth() const{
        return ownShip.getDepth();
    }

    irr::f32 SimulationModel::getWaveHeight(irr::f32 posX, irr::f32 posZ) const {
        return water.getWaveHeight(posX,posZ);
    }

    irr::core::vector2df SimulationModel::getLocalNormals(irr::f32 relPosX, irr::f32 relPosZ) const {
        return water.getLocalNormals(relPosX,relPosZ);
    }

    irr::core::vector2df SimulationModel::getTidalStream(irr::f32 longitude, irr::f32 latitude, uint64_t absoluteTime) const {
        return tide.getTidalStream(longitude,latitude,absoluteTime);
    }

   // void SimulationModel::getTime(irr::u8& hour, irr::u8& min, irr::u8& sec) const{
   //    //FIXME: Complete
   // }

    //void SimulationModel::getDate(irr::u8& day, irr::u8& month, irr::u16& year) const{
    //    //FIXME: Complete
    //}

    uint64_t SimulationModel::getTimestamp() const{
        return absoluteTime;
    }

    uint64_t SimulationModel::getTimeOffset() const { //The timestamp at the start of the first day of the scenario
        return scenarioOffsetTime;
    }

    void SimulationModel::setTimeDelta(irr::f32 scenarioTime) {
        this->scenarioTime = scenarioTime;
    }

    irr::f32 SimulationModel::getTimeDelta() const { //The change in time (s) since the start of the start day of the scenario
        return scenarioTime;
    }

    irr::u32 SimulationModel::getNumberOfOtherShips() const {
        return otherShips.getNumber();
    }

    irr::u32 SimulationModel::getNumberOfBuoys() const {
        return buoys.getNumber();
    }

    std::string SimulationModel::getOtherShipName(int number) const{
        return otherShips.getName(number);
    }

    irr::f32 SimulationModel::getOtherShipPosX(int number) const{
        return otherShips.getPosition(number).X + offsetPosition.X;
    }

    irr::f32 SimulationModel::getOtherShipPosZ(int number) const{
        return otherShips.getPosition(number).Z + offsetPosition.Z;
    }

    irr::f32 SimulationModel::getOtherShipHeading(int number) const{
        return otherShips.getHeading(number);
    }

    irr::f32 SimulationModel::getOtherShipSpeed(int number) const{
        return otherShips.getSpeed(number);
    }

    irr::u32 SimulationModel::getOtherShipMMSI(int number) const{
        return otherShips.getMMSI(number);
    }

    void SimulationModel::setOtherShipMMSI(int number, irr::u32 mmsi) {
        otherShips.setMMSI(number,mmsi);
    }

    void SimulationModel::setOtherShipHeading(int number, irr::f32 hdg){
        otherShips.setHeading(number, hdg);
    }

    void SimulationModel::setOtherShipSpeed(int number, irr::f32 speed){
        otherShips.setSpeed(number, speed);
    }

    void SimulationModel::setOtherShipPos(int number, irr::f32 positionX, irr::f32 positionZ){
        otherShips.setPos(number, positionX - offsetPosition.X, positionZ - offsetPosition.Z);
    }

    std::vector<Leg> SimulationModel::getOtherShipLegs(int number) const{
        return otherShips.getLegs(number);
    }

    irr::f32 SimulationModel::getBuoyPosX(int number) const{
        return buoys.getPosition(number).X + offsetPosition.X;
    }

    irr::f32 SimulationModel::getBuoyPosZ(int number) const{
        return buoys.getPosition(number).Z + offsetPosition.Z;
    }

    void SimulationModel::changeOtherShipLeg(int shipNumber, int legNumber, irr::f32 bearing, irr::f32 speed, irr::f32 distance) {
        otherShips.changeLeg(shipNumber, legNumber, bearing, speed, distance, scenarioTime);
    }

    void SimulationModel::addOtherShipLeg(int shipNumber, int afterLegNumber, irr::f32 bearing, irr::f32 speed, irr::f32 distance) {
        otherShips.addLeg(shipNumber, afterLegNumber, bearing, speed, distance, scenarioTime);
    }

    void SimulationModel::deleteOtherShipLeg(int shipNumber, int legNumber) {
        otherShips.deleteLeg(shipNumber, legNumber, scenarioTime);
    }

    void SimulationModel::resetOtherShipLegs(int shipNumber, irr::f32 course, irr::f32 speedKts, irr::f32 distanceNm) {
        otherShips.resetLegs(shipNumber, course, speedKts, distanceNm, scenarioTime);
    }

	std::string SimulationModel::getOwnShipEngineSound() const {

		//Check existence of sound file in base path, and if not fall back to default.
		std::string soundPath = ownShip.getBasePath();

		{ //Create local scope for file
            soundPath.append("/Engine.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Check for lower case version
		{
            soundPath = ownShip.getBasePath();
            soundPath.append("/engine.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Fall back to default, again checking both upper and lower case

		{
            soundPath = "Sounds/Engine.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		{
            soundPath = "Sounds/engine.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//In case nothing found
		return "";

	}

	std::string SimulationModel::getOwnShipWaveSound() const {

		//Check existence of sound file in base path, and if not fall back to default.
		std::string soundPath = ownShip.getBasePath();

		{ //Create local scope for file
            soundPath.append("/Bwave.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Check for lower case version
		{
            soundPath = ownShip.getBasePath();
            soundPath.append("/bwave.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Fall back to default, again checking both upper and lower case

		{
            soundPath = "Sounds/Bwave.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		{
            soundPath = "Sounds/bwave.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//In case nothing found
		return "";

	}

	std::string SimulationModel::getOwnShipHornSound() const {

		//Check existence of sound file in base path, and if not fall back to default.
		std::string soundPath = ownShip.getBasePath();

		{ //Create local scope for file
            soundPath.append("/Horn.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Check for lower case version
		{
            soundPath = ownShip.getBasePath();
            soundPath.append("/horn.wav");
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//Fall back to default, again checking both upper and lower case

		{
            soundPath = "Sounds/Horn.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		{
            soundPath = "Sounds/horn.wav";
            std::ifstream file(soundPath.c_str());
            if (file.good()) {
                return soundPath;
            }
		}

		//In case nothing found
		return "";

	}

    void SimulationModel::setHeading(irr::f32 hdg)
    {
         ownShip.setHeading(hdg);
    }

    irr::f32 SimulationModel::getRateOfTurn() const
    {
        return ownShip.getRateOfTurn();
    }

    void SimulationModel::setRateOfTurn(irr::f32 rateOfTurn)
    {
        ownShip.setRateOfTurn(rateOfTurn);
    }

    void SimulationModel::setPos(irr::f32 positionX, irr::f32 positionZ)
    {
        ownShip.setPosition(positionX - offsetPosition.X, positionZ - offsetPosition.Z );
    }


    irr::f32 SimulationModel::getHeading() const
    {
        return(ownShip.getHeading());
    }

    void SimulationModel::setRudder(irr::f32 rudder)
    {
        //Set the rudder (-ve is port, +ve is stbd)
        ownShip.setRudder(rudder);
    }

    irr::f32 SimulationModel::getRudder() const
    {
        return ownShip.getRudder();
    }


// DEE vvvvvvvvvvv
    void SimulationModel::setWheel(irr::f32 wheel)
    {
        //Set the wheel (-ve is port, +ve is stbd)
        ownShip.setWheel(wheel);
    }

    irr::f32 SimulationModel::getWheel() const
    {
        return ownShip.getWheel();
    }
// DEE ^^^^^^^^^^^


    void SimulationModel::setPortEngine(irr::f32 port)
    {
        //Set the engine, (-ve astern, +ve ahead)
        ownShip.setPortEngine(port); //This method limits the range applied

		//Set engine sound level
		if (ownShip.isSingleEngine()) {
			sound->setVolumeEngine(fabs(getPortEngine())*0.5);
		}
		else {
			sound->setVolumeEngine((fabs(getPortEngine()) + fabs(getStbdEngine()))*0.5);
		}

    }

    void SimulationModel::setStbdEngine(irr::f32 stbd)
    {
        //Set the engine, (-ve astern, +ve ahead)
        ownShip.setStbdEngine(stbd); //This method limits the range applied

		//Set engine sound level
		sound->setVolumeEngine((fabs(getPortEngine()) + fabs(getStbdEngine()))*0.5);
    }

    irr::f32 SimulationModel::getPortEngine() const
    {
        return ownShip.getPortEngine();

    }

    irr::f32 SimulationModel::getStbdEngine() const
    {
        return ownShip.getStbdEngine();
    }

    irr::f32 SimulationModel::getPortEngineRPM() const
    {
        return ownShip.getPortEngineRPM();
    }

    irr::f32 SimulationModel::getStbdEngineRPM() const
    {
        return ownShip.getStbdEngineRPM();
    }

    void SimulationModel::setBowThruster(irr::f32 proportion)
    {
        ownShip.setBowThruster(proportion);
    }

    void SimulationModel::setSternThruster(irr::f32 proportion)
    {
        ownShip.setSternThruster(proportion);
    }

    void SimulationModel::setBowThrusterRate(irr::f32 bowThrusterRate){
        //Sets the rate of increase of bow thruster, used for joystick button control
        ownShip.setBowThrusterRate(bowThrusterRate);
    }

    void SimulationModel::setSternThrusterRate(irr::f32 sternThrusterRate){
        //Sets the rate of increase of bow thruster, used for joystick button control
        ownShip.setSternThrusterRate(sternThrusterRate);
    }

    irr::f32 SimulationModel::getBowThruster() const
    {
        return ownShip.getBowThruster();
    }

    irr::f32 SimulationModel::getSternThruster() const
    {
        return ownShip.getSternThruster();
    }

    void SimulationModel::setAccelerator(irr::f32 accelerator)
    {
        device->getTimer()->setSpeed(accelerator);
    }

    irr::f32 SimulationModel::getAccelerator() const
    {
        return device->getTimer()->getSpeed();
    }

    void SimulationModel::setWeather(irr::f32 weather)
    {
        this->weather = weather;
    }

    irr::f32 SimulationModel::getWeather() const
    {
        return weather;
    }

    void SimulationModel::setRain(irr::f32 rainIntensity)
    {
        this->rainIntensity = rainIntensity;
    }

    irr::f32 SimulationModel::getRain() const
    {
        return rainIntensity;
    }

    void SimulationModel::setVisibility(irr::f32 visibilityNm)
    {
        this->visibilityRange = visibilityNm;
    }

    irr::f32 SimulationModel::getVisibility() const
    {
        return visibilityRange;
    }

    void SimulationModel::setWaterVisible(bool visible)
    {
        water.setVisible(visible);
    }

    void SimulationModel::lookUp()
    {
        camera.lookUp();
    }

    void SimulationModel::lookDown()
    {
        camera.lookDown();
    }

    void SimulationModel::lookLeft()
    {
        camera.lookLeft();
    }

    void SimulationModel::lookRight()
    {
        camera.lookRight();
    }

    void SimulationModel::setPanSpeed(irr::f32 horizontalPanSpeed)
    {
        camera.setPanSpeed(horizontalPanSpeed);
    }

    void SimulationModel::setVerticalPanSpeed(irr::f32 verticalPanSpeed)
    {
        camera.setVerticalPanSpeed(verticalPanSpeed);
    }

    void SimulationModel::changeLookPx(irr::s32 deltaX, irr::s32 deltaY)
    {
        irr::f32 proportionalX = deltaX/(irr::f32)driver->getScreenSize().Width;
        irr::f32 proportionalY = deltaY/(irr::f32)driver->getScreenSize().Width;
        camera.lookChange(proportionalX,proportionalY);
    }

    void SimulationModel::lookStepLeft()
    {
        camera.lookStepLeft();
    }

    void SimulationModel::lookStepRight()
    {
        camera.lookStepRight();
    }

    void SimulationModel::moveCameraForwards()
    {
        camera.moveForwards();
    }

    void SimulationModel::moveCameraBackwards()
    {
        camera.moveBackwards();
    }

    void SimulationModel::lookAhead()
    {
        camera.lookAhead();
    }

    void SimulationModel::lookAstern()
    {
        camera.lookAstern();
    }

    void SimulationModel::lookPort()
    {
        camera.lookPort();
    }

    void SimulationModel::lookStbd()
    {
        camera.lookStbd();
    }

    void SimulationModel::changeView()
    {
        camera.changeView();
        ownShip.setViewVisibility(camera.getView());
    }

    void SimulationModel::setView(irr::u32 view)
    {
        camera.setView(view);
        ownShip.setViewVisibility(camera.getView());
    }

    irr::u32 SimulationModel::getCameraView() const
    {
        return camera.getView();
    }

	void SimulationModel::toggleRadarOn()
	{
		radarCalculation.toggleRadarOn();
	}

    bool SimulationModel::isRadarOn() const
    {
        return radarCalculation.isRadarOn();
    }

    void SimulationModel::increaseRadarRange()
    {
        radarCalculation.increaseRange();
    }

    void SimulationModel::decreaseRadarRange()
    {
        radarCalculation.decreaseRange();
    }

    void SimulationModel::setRadarGain(irr::f32 value)
    {
        radarCalculation.setGain(value);
    }

    void SimulationModel::setRadarClutter(irr::f32 value)
    {
        radarCalculation.setClutter(value);
    }

    void SimulationModel::setRadarRain(irr::f32 value)
    {
        radarCalculation.setRainClutter(value);
    }

    void SimulationModel::setPIData(irr::s32 PIid, irr::f32 PIbearing, irr::f32 PIrange)
    {
        radarCalculation.setPIData(PIid, PIbearing, PIrange);
    }

    irr::f32 SimulationModel::getPIbearing(irr::s32 PIid) const
    {
        return radarCalculation.getPIbearing(PIid);
    }

    irr::f32 SimulationModel::getPIrange(irr::s32 PIid) const
    {
        return radarCalculation.getPIrange(PIid);
    }

    void SimulationModel::increaseRadarEBLRange() {radarCalculation.increaseEBLRange();}
    void SimulationModel::decreaseRadarEBLRange() {radarCalculation.decreaseEBLRange();}
    void SimulationModel::increaseRadarEBLBrg() {radarCalculation.increaseEBLBrg();}
    void SimulationModel::decreaseRadarEBLBrg() {radarCalculation.decreaseEBLBrg();}

    void SimulationModel::setRadarNorthUp()
    {
        radarCalculation.setNorthUp();
    }

    void SimulationModel::setRadarCourseUp()
    {
        radarCalculation.setCourseUp();
    }

    void SimulationModel::setRadarHeadUp()
    {
        radarCalculation.setHeadUp();
    }

    void SimulationModel::setArpaOn(bool on)
    {
        radarCalculation.setArpaOn(on);
    }

    void SimulationModel::setRadarARPARel()
    {
        radarCalculation.setRadarARPARel();
    }

    void SimulationModel::setRadarARPATrue()
    {
        radarCalculation.setRadarARPATrue();
    }

    void SimulationModel::setRadarARPAVectors(irr::f32 vectorMinutes)
    {
        radarCalculation.setRadarARPAVectors(vectorMinutes);
    }

    void SimulationModel::setRadarDisplayRadius(irr::u32 radiusPx)
    {
        radarCalculation.setRadarDisplayRadius(radiusPx);
        radarScreen.setRadarDisplayRadius(radiusPx);
    }

    irr::u32 SimulationModel::getARPATracks() const
    {
        return radarCalculation.getARPATracks();
    }

    ARPAContact SimulationModel::getARPATrack(irr::u32 index) const
    {
        return radarCalculation.getARPATrack(index);
    }

    void SimulationModel::setMainCameraActive()
    {
        camera.setActive();
    }

    void SimulationModel::setRadarCameraActive()
    {
        radarCamera.setActive();
    }

    void SimulationModel::setZoom(bool zoomOn)
    {
        if (zoomOn) {
            zoom = 7.0; //Binoculars magnification
        } else {
            zoom = 1.0;
        }
        camera.setHFOV(irr::core::degToRad(viewAngle)/zoom);
    }

    void SimulationModel::setMouseDown(bool isMouseDown)
    {
        this->isMouseDown = isMouseDown;
    }

    void SimulationModel::updateViewport(irr::f32 aspect)
    {
        camera.updateViewport(aspect);
    }

    irr::u32 SimulationModel::getLoopNumber() const
    {
        return loopNumber;
    }

    std::string SimulationModel::getSerialisedScenario() const
    {
        return serialisedScenarioData;
    }

    std::string SimulationModel::getScenarioName() const
    {
        return scenarioName;
    }

    std::string SimulationModel::getWorldName() const
    {
        return worldName;
    }

    void SimulationModel::releaseManOverboard()
    {
        //Only release/update if not already released
        if (!manOverboard.getVisible()) {
            manOverboard.setVisible(true);
            irr::core::vector3df ownShipPos = ownShip.getPosition();
            irr::core::vector3df relativePosition;
            relativePosition.Y = 0;
            //Put randomly on port or starboard side of the ship
            if (rand() > RAND_MAX/2) {
                relativePosition.X = ownShip.getWidth() *  0.6 * cos(ownShip.getHeading()*irr::core::DEGTORAD);
                relativePosition.Z = ownShip.getWidth() * -0.6 * sin(ownShip.getHeading()*irr::core::DEGTORAD);
                //PositionEntity(mob,EntityX( ship_parent )+(OwnShipWidth#*0.6)*Cos(angle#),THeight#,EntityZ( ship_parent )-(OwnShipWidth#*0.6)*Sin(angle#), True)
            } else {
                relativePosition.X = ownShip.getWidth() * -0.6 * cos(ownShip.getHeading()*irr::core::DEGTORAD);
                relativePosition.Z = ownShip.getWidth() *  0.6 * sin(ownShip.getHeading()*irr::core::DEGTORAD);
                //PositionEntity(mob,EntityX( ship_parent )-(OwnShipWidth#*0.6)*Cos(angle#),THeight#,EntityZ( ship_parent )+(OwnShipWidth#*0.6)*Sin(angle#), True)
            }
            manOverboard.setPosition(ownShipPos + relativePosition);
        }

    }

    void SimulationModel::retrieveManOverboard()
    {
        manOverboard.setVisible(false);
    }

    bool SimulationModel::getManOverboardVisible() const
    {
        return manOverboard.getVisible();
    }

    irr::f32 SimulationModel::getManOverboardPosX() const
    {
        return manOverboard.getPosition().X + offsetPosition.X;
    }

    irr::f32 SimulationModel::getManOverboardPosZ() const
    {
        return manOverboard.getPosition().Z + offsetPosition.Z;
    }


    void SimulationModel::setManOverboardVisible(bool visible)
    {
        //To be used directly, eg when in secondary display mode only
        manOverboard.setVisible(visible);
    }

    void SimulationModel::setManOverboardPos(irr::f32 positionX, irr::f32 positionZ)
    {
        //To be used directly, eg when in secondary display mode only
        manOverboard.setPosition(irr::core::vector3df(positionX - offsetPosition.X,0,positionZ - offsetPosition.Z));
    }

    bool SimulationModel::hasGPS() const
    {
        return ownShip.hasGPS();
    }

    bool SimulationModel::isSingleEngine() const
    {
        return ownShip.isSingleEngine();
    }

    bool SimulationModel::hasDepthSounder() const
    {
        return ownShip.hasDepthSounder();
    }

    bool SimulationModel::hasBowThruster() const
    {
        return ownShip.hasBowThruster();
    }

    bool SimulationModel::hasSternThruster() const
    {
        return ownShip.hasSternThruster();
    }

    bool SimulationModel::hasTurnIndicator() const
    {
        return ownShip.hasTurnIndicator();
    }

    irr::f32 SimulationModel::getMaxSounderDepth() const
    {
        return ownShip.getMaxSounderDepth();
    }

	void SimulationModel::startHorn() {
		sound->setVolumeHorn(1.0);
	}

	void SimulationModel::endHorn() {
		sound->setVolumeHorn(0.0);
	}

    void SimulationModel::update()
    {

        #ifdef WITH_PROFILING
        IPROF_FUNC;
        #endif
// DEE vvvv debug I think that this is effectively the CLOCK

        //Declare here, so scope added as part of profiling isn't a problem
        irr::u32 lightLevel;
        irr::f32 elevAngle;
        irr::core::vector2di cursorPositionRadar;
        std::vector<irr::f32> CPAs;
        std::vector<irr::f32> TCPAs;
		std::vector<irr::f32> headings;
		std::vector<irr::f32> speeds;
        bool paused;
        bool collided;

        { IPROF("Increment time");

        // move time along .. this goes before everything else in the cycle

        //get delta time
        currentTime = device->getTimer()->getTime();
        deltaTime = (currentTime - previousTime)/1000.f;
        //deltaTime = (currentTime - previousTime)/1000.f;
        previousTime = currentTime;

        //add this to the scenario time
        scenarioTime += deltaTime;
        absoluteTime = Utilities::round(scenarioTime) + scenarioOffsetTime;

        //increment loop number
        loopNumber++;

        // end move time along
        }{ IPROF("Set radar display radius");


        //Ensure we have the right radar screen resolution
        setRadarDisplayRadius(guiMain->getRadarPixelRadius());

        }{ IPROF("Update tide");

        //Update tide height and tidal stream here.
        tide.update(absoluteTime);
        tideHeight = tide.getTideHeight();

        }{ IPROF("Update lighting");

        //update ambient lighting
        light.update(scenarioTime);
        //Note that linear fog is hardcoded into the water shader, so should be changed there if we use other fog types
        driver->setFog(light.getLightSColor(), irr::video::EFT_FOG_LINEAR , 0.01*visibilityRange*M_IN_NM, visibilityRange*M_IN_NM, 0.00003f /*exp fog parameter*/, true, true);
        lightLevel = light.getLightLevel();

        }{ IPROF("Update rain");
        //update rain
        rain.setIntensity(rainIntensity);
        rain.update(scenarioTime);

        }{ IPROF("Update other ships");
        //update other ship positions etc
        otherShips.update(deltaTime,scenarioTime,tideHeight,lightLevel); //Update other ship motion (based on leg information), and light visibility.

        }{ IPROF("Update buoys");
        //update buoys (for lights)
        buoys.update(deltaTime,scenarioTime,tideHeight,lightLevel);

        }{ IPROF("Update land lights");
        //Update land lights
        landLights.update(deltaTime,scenarioTime,lightLevel);

        }{ IPROF("Update own ship");
        //update own ship
        ownShip.update(deltaTime, scenarioTime, tideHeight, weather);

        }{ IPROF("Update MOB");
        //update man overboard
        manOverboard.update(deltaTime, tideHeight);

        }{ IPROF("Check for collisions");
        //Check for collisions
        collided = checkOwnShipCollision();

        }{ IPROF("Update water pos");
        //update water position
        water.update(tideHeight,camera.getPosition(),light.getLightLevel(), weather);

        }{ IPROF("Normalise ");
        //Normalise positions if required (More than 1000 metres from origin)
        //FIXME: TEMPORARY MODS WITH REALISTICWATERSCENENODE
        if(ownShip.getPosition().getLength() > 1000) {
            irr::core::vector3df ownShipPos = ownShip.getPosition();
            irr::s32 deltaX = -1*(irr::s32)ownShipPos.X;
            irr::s32 deltaZ = -1*(irr::s32)ownShipPos.Z;
            //Round to nearest 1000 metres - (multiple of water tile width, to avoid jumps here)
            deltaX = 500.0*Utilities::round(deltaX/500.0);
            deltaZ = 500.0*Utilities::round(deltaZ/500.0);

            //Move all objects
            ownShip.moveNode(deltaX,0,deltaZ);
            terrain.moveNode(deltaX,0,deltaZ); //SLOW!
            otherShips.moveNode(deltaX,0,deltaZ);
            buoys.moveNode(deltaX,0,deltaZ);
            landObjects.moveNode(deltaX,0,deltaZ);
            landLights.moveNode(deltaX,0,deltaZ);
            manOverboard.moveNode(deltaX,0,deltaZ);

            //Change stored offset
            offsetPosition.X -= deltaX;
            offsetPosition.Z -= deltaZ;

            std::string normalisedLogMessage = "Normalised, offset X: ";
            normalisedLogMessage.append(Utilities::lexical_cast<std::string>(offsetPosition.X));
            normalisedLogMessage.append(" Z: ");
            normalisedLogMessage.append(Utilities::lexical_cast<std::string>(offsetPosition.Z));
            device->getLogger()->log(normalisedLogMessage.c_str());

            //Debugging
            //std::cout << normalisedLogMessage << std::endl;

        }
        }{ IPROF("Update camera pos");

        //update the camera position
        camera.update(deltaTime);

        }
        if (radarCalculation.isRadarOn()) {
            { IPROF("Update radar cursor position");
            //set radar screen position, and update it with a radar image from the radar calculation
            cursorPositionRadar = guiMain->getCursorPositionRadar();
            }{ IPROF("Update radar calculation");
            //Choose which radar images to use, depending on the size of the display being used
            if (2*guiMain->getRadarPixelRadius() > radarImage->getDimension().Width) {
                radarImageChosen = radarImageLarge;
                radarImageOverlaidChosen = radarImageOverlaidLarge;
            } else {
                radarImageChosen = radarImage;
                radarImageOverlaidChosen = radarImageOverlaid;
            }
            radarCalculation.update(radarImageChosen,radarImageOverlaidChosen,offsetPosition,terrain,ownShip,buoys,otherShips,weather,rainIntensity,tideHeight,deltaTime,absoluteTime,cursorPositionRadar,isMouseDown);
            }{ IPROF("Update radar screen");
            radarScreen.update(radarImageOverlaidChosen);
            }{ IPROF("Update radar camera");
            radarCamera.update();
            }
        } else {
            radarScreen.getSceneNode()->setVisible(false);
        }
        { IPROF("Check if paused ");
        //check if paused
        paused = device->getTimer()->getSpeed()==0.0;

        }{ IPROF("Calc elevation etc ");
        //calculate current angular elevation due to pitch and roll in the view direction
        irr::f32 lookRadians = irr::core::degToRad(camera.getLook());
        elevAngle = -1*ownShip.getPitch()*cos(lookRadians) + ownShip.getRoll()*sin(lookRadians) + camera.getLookUp();

        }{ IPROF("Get radar ARPA data");
        //get radar ARPA data to show
        irr::u32 numberOfARPATracks = radarCalculation.getARPATracks();
        for(unsigned int i = 0; i<numberOfARPATracks; i++) {
            ARPAEstimatedState state = radarCalculation.getARPATrack(i).estimate;
            CPAs.push_back(state.cpa);
            TCPAs.push_back(state.tcpa);
			headings.push_back(state.absHeading);
			speeds.push_back(state.speed);
        }


        }{ IPROF("Collate GUI data ");

        //Collate data to show in gui
        guiData->lat = getLat();
        guiData->longitude = getLong();
        guiData->hdg = ownShip.getHeading();
        guiData->viewAngle = camera.getLook();
        guiData->viewElevationAngle = elevAngle;
        guiData->spd = ownShip.getSpeed();
        guiData->portEng = ownShip.getPortEngine();
        guiData->stbdEng = ownShip.getStbdEngine();
        guiData->rudder = ownShip.getRudder();  // inner workings of this will be modified in model DEE
        guiData->wheel = ownShip.getWheel();    // inner workings of this will be modified in model DEE
        guiData->bowThruster = ownShip.getBowThruster();
        guiData->sternThruster = ownShip.getSternThruster();
        guiData->depth = ownShip.getDepth();
        guiData->weather = weather;
        guiData->rain = rainIntensity;
        guiData->visibility = visibilityRange;
        guiData->radarRangeNm = radarCalculation.getRangeNm();
        guiData->radarGain = radarCalculation.getGain();
        guiData->radarClutter = radarCalculation.getClutter();
        guiData->radarRain = radarCalculation.getRainClutter();
        guiData->guiRadarEBLBrg = radarCalculation.getEBLBrg();
        guiData->guiRadarEBLRangeNm = radarCalculation.getEBLRangeNm();
        guiData->CPAs = CPAs;
        guiData->TCPAs = TCPAs;
		guiData->headings = headings;
		guiData->speeds = speeds;
        guiData->currentTime = Utilities::timestampToString(absoluteTime);
        guiData->paused = paused;
        guiData->collided = collided;
        guiData->headUp = radarCalculation.getHeadUp();
        guiData->radarOn = radarCalculation.isRadarOn();

// DEE vvvv units are rad per second
	guiData->RateOfTurn = ownShip.getRateOfTurn();
// DEE ^^^^
        }{ IPROF("Update gui data");
        //send data to gui
        guiMain->updateGuiData(guiData); //Set GUI heading in degrees and speed (in m/s)
        }
    }

    bool SimulationModel::checkOwnShipCollision()
    {

        return (ownShip.isBuoyCollision() || ownShip.isOtherShipCollision());

        /*

        irr::u32 numberOfOtherShips = otherShips.getNumber();
        irr::u32 numberOfBuoys = buoys.getNumber();

        irr::core::vector3df thisShipPosition = ownShip.getPosition();
        irr::f32 thisShipLength = ownShip.getLength();
        irr::f32 thisShipWidth = ownShip.getWidth();
        irr::f32 thisShipHeading = ownShip.getHeading();

        for (irr::u32 i = 0; i<numberOfOtherShips; i++) {
            irr::core::vector3df otherPosition = otherShips.getPosition(i);
            irr::f32 otherShipLength = otherShips.getLength(i);
            irr::f32 otherShipWidth = otherShips.getWidth(i);
            irr::f32 otherShipHeading = otherShips.getHeading(i);

            irr::core::vector3df relPosition = otherPosition - thisShipPosition;
            irr::f32 distanceToShip = relPosition.getLength();
            irr::f32 bearingToOtherShipDeg = irr::core::radToDeg(atan2(relPosition.X, relPosition.Z));

            //Bearings relative to ship's head (from this ship and from other)
            irr::f32 relativeBearingOwnShip = bearingToOtherShipDeg - thisShipHeading;
            irr::f32 relativeBearingOtherShip = 180 + bearingToOtherShipDeg - otherShipHeading;

            //Find the minimum distance before a collision occurs
            irr::f32 minDistanceOwn = 0.5*fabs(thisShipWidth*sin(irr::core::degToRad(relativeBearingOwnShip))) + 0.5*fabs(thisShipLength*cos(irr::core::degToRad(relativeBearingOwnShip)));
            irr::f32 minDistanceOther = 0.5*fabs(otherShipWidth*sin(irr::core::degToRad(relativeBearingOtherShip))) + 0.5*fabs(otherShipLength*cos(irr::core::degToRad(relativeBearingOtherShip)));
            irr::f32 minDistance = minDistanceOther + minDistanceOwn;

            if (distanceToShip < minDistance) {
                return true;
            }
        }

        for (irr::u32 i = 0; i<numberOfBuoys; i++) { //Collision with buoy
            irr::core::vector3df otherPosition = buoys.getPosition(i);

            irr::core::vector3df relPosition = otherPosition - thisShipPosition;
            irr::f32 distanceToBuoy = relPosition.getLength();
            irr::f32 bearingToBuoyDeg = irr::core::radToDeg(atan2(relPosition.X, relPosition.Z));

            //Bearings relative to ship's head (from this ship and from other)
            irr::f32 relativeBearingOwnShip = bearingToBuoyDeg - thisShipHeading;

            //Find the minimum distance before a collision occurs
            irr::f32 minDistanceOwn = 0.5*fabs(thisShipWidth*sin(irr::core::degToRad(relativeBearingOwnShip))) + 0.5*fabs(thisShipLength*cos(irr::core::degToRad(relativeBearingOwnShip)));

            if (distanceToBuoy < minDistanceOwn) {
                return true;
            }
        }

        return false; //If no collision has been found
        */
    }

