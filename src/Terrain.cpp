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

//NOTE: This uses a modified version of Irrlicht for terrain loading, which loads the terrain natively rotated
//180 degrees compared to standard Irrlicht 1.8.1.

#include "Terrain.hpp"

#include "IniFile.hpp"
#include "Constants.hpp"
#include "Utilities.hpp"

#include "BCTerrainSceneNode.h"

#include <iostream>

//using namespace irr;

Terrain::Terrain()
{

}

Terrain::~Terrain()
{
    //dtor
    for (unsigned int i=0; i<terrains.size(); i++) {
        terrains.at(i)->drop();
    }
}

void Terrain::load(const std::string& worldPath, irr::scene::ISceneManager* smgr, irr::IrrlichtDevice* device)
{

    dev = device;
    
    irr::video::IVideoDriver* driver = smgr->getVideoDriver();

    //Get full path to the main Terrain.ini file
    std::string worldTerrainFile = worldPath;
    worldTerrainFile.append("/terrain.ini");

    irr::u32 numberOfTerrains = IniFile::iniFileTou32(worldTerrainFile, "Number");
    if (numberOfTerrains <= 0) {
        std::cerr << "Could not load terrain. No terrain defined in settings file." << std::endl;
        exit(EXIT_FAILURE);
    }

    for (unsigned int i = 1; i<=numberOfTerrains; i++) {


        irr::f32 terrainLong = IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainLong",i));
        irr::f32 terrainLat = IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainLat",i));
        irr::f32 terrainLongExtent = IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainLongExtent",i));
        irr::f32 terrainLatExtent = IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainLatExtent",i));

        irr::f32 terrainMaxHeight=IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainMaxHeight",i));
        irr::f32 seaMaxDepth=IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("SeaMaxDepth",i));
        //irr::f32 terrainHeightMapSize=IniFile::iniFileTof32(worldTerrainFile, IniFile::enumerate1("TerrainHeightMapSize",i));

        std::string heightMapName = IniFile::iniFileToString(worldTerrainFile, IniFile::enumerate1("HeightMap",i));
        std::string textureMapName = IniFile::iniFileToString(worldTerrainFile, IniFile::enumerate1("Texture",i));

        bool usesRGBEncoding = IniFile::iniFileTou32(worldTerrainFile, IniFile::enumerate1("UsesRGB",i)) > 0;

        //Terrain dimensions in metres
        irr::f32 terrainXWidth = terrainLongExtent * 2.0 * PI * EARTH_RAD_M * cos( irr::core::degToRad(terrainLat + terrainLatExtent/2.0)) / 360.0;
        irr::f32 terrainZWidth = terrainLatExtent  * 2.0 * PI * EARTH_RAD_M / 360;

        //calculations just needed for terrain loading
        //irr::f32 scaleX = terrainXWidth / (terrainHeightMapSize);
        irr::f32 scaleY = (terrainMaxHeight + seaMaxDepth)/ (255.0);
        //irr::f32 scaleZ = terrainZWidth / (terrainHeightMapSize);
        irr::f32 terrainY = -1*seaMaxDepth;

        //Full paths
        std::string heightMapPath = worldPath;
        heightMapPath.append("/");
        heightMapPath.append(heightMapName);
        std::string textureMapPath = worldPath;
        textureMapPath.append("/");
        textureMapPath.append(textureMapName);

        //Fixme: Could also check that the terrain is now 2^n + 1 square (was 2^n in B3d version)
        //Add an empty terrain
        //irr::scene::ITerrainSceneNode* terrain = smgr->addTerrainSceneNode("",0,-1,irr::core::vector3df(0.f, terrainY, 0.f),irr::core::vector3df(0.f, 0.f, 0.f),irr::core::vector3df(1,1,1),irr::video::SColor(255,255,255,255),5,irr::scene::ETPS_9,0,true);

        irr::scene::BCTerrainSceneNode* terrain = new irr::scene::BCTerrainSceneNode(
            device,
            smgr->getRootSceneNode(), 
            smgr,
			smgr->getFileSystem(), -1, 5, irr::scene::ETPS_33
        );

        //Load the map
        irr::io::IReadFile* heightMapFile = smgr->getFileSystem()->createAndOpenFile(heightMapPath.c_str());
        //Check the height map file has loaded and the terrain exists
        if (terrain==0 || heightMapFile == 0) {
            //Could not load terrain
            std::cerr << "Could not load terrain. Height map file not loaded. " << heightMapPath << std::endl;
            std::cerr << "Terrain: " << terrain << " heightMapFile: " << heightMapFile << std::endl;
            exit(EXIT_FAILURE);
        }

        //Load the terrain and check success
        bool loaded = false;
        //Check if extension is .irr::f32 for binary floating point file
        std::string extension = "";
        if (heightMapName.length() > 3) {
            extension = heightMapName.substr(heightMapName.length() - 4,4);
            Utilities::to_lower(extension);
        }
        if (extension.compare(".f32") == 0 ) {
            //Binary file
            loaded = terrain->loadHeightMapRAW(heightMapFile,32,true,true);
        } else {
            //Normal image file
            //loaded = terrain->loadHeightMap(heightMapFile,irr::video::SColor(255, 255, 255, 255), 0, usesRGBEncoding);
            loaded = terrain->loadHeightMapVector(heightMapImageToVector(heightMapFile,usesRGBEncoding,smgr),irr::video::SColor(255, 255, 255, 255), 0);
        }

        if (!loaded) {
            //Could not load terrain
            std::cerr << "Could not load terrain at loadHeightMap stage." << std::endl;
            exit(EXIT_FAILURE);
        }

        irr::f32 scaleX = terrainXWidth/(terrain->getBoundingBox().MaxEdge.X - terrain->getBoundingBox().MinEdge.X);
        irr::f32 scaleZ = terrainZWidth/(terrain->getBoundingBox().MaxEdge.Z - terrain->getBoundingBox().MinEdge.Z);
            
        if (extension.compare(".f32") == 0 || usesRGBEncoding) {
            //Set scales etc to be 1.0, so heights are used directly
            terrain->setScale(irr::core::vector3df(scaleX,1.0f,scaleZ));
            terrain->setPosition(irr::core::vector3df(0.f, 0.f, 0.f));
        } else {
            //Normal heightmap, so use scale from terrainMaxHeight etc
            terrain->setScale(irr::core::vector3df(scaleX,scaleY,scaleZ));
            terrain->setPosition(irr::core::vector3df(0.f, terrainY, 0.f));
        }

        heightMapFile->drop();
        //TODO: Do we need to drop terrain?

        terrain->setMaterialFlag(irr::video::EMF_FOG_ENABLE, true);
        terrain->setMaterialFlag(irr::video::EMF_NORMALIZE_NORMALS, true); //Normalise normals on scaled meshes, for correct lighting
        //Todo: Anti-aliasing flag?
        terrain->setMaterialTexture(0, driver->getTexture(textureMapPath.c_str()));

        if (i==1) {
            //Private member variables used in further calculations
            primeTerrainLong = terrainLong;
            primeTerrainXWidth = terrainXWidth;
            primeTerrainLongExtent = terrainLongExtent;
            primeTerrainLat = terrainLat;
            primeTerrainZWidth = terrainZWidth;
            primeTerrainLatExtent = terrainLatExtent;


        } else {
            //Non-primary terrains need to be moved to account for their position (primary terrain starts at 0,0)

            irr::core::vector3df currentPos = terrain->getPosition();
            irr::f32 deltaX = (terrainLong - primeTerrainLong) * primeTerrainXWidth / primeTerrainLongExtent;
            irr::f32 deltaZ = (terrainLat - primeTerrainLat) * primeTerrainZWidth / primeTerrainLatExtent;
            irr::f32 newPosX = currentPos.X + deltaX;
            irr::f32 newPosY = currentPos.Y;
            irr::f32 newPosZ = currentPos.Z + deltaZ;
            terrain->setPosition(irr::core::vector3df(newPosX,newPosY,newPosZ));
        }

        terrains.push_back(terrain);

    }


}

std::vector<std::vector<irr::f32>> Terrain::heightMapImageToVector(irr::io::IReadFile* heightMapFile, bool usesRGBEncoding, irr::scene::ISceneManager* smgr)
{
    irr::video::IImage* heightMap = smgr->getVideoDriver()->createImageFromFile(heightMapFile);
    std::vector<std::vector<irr::f32>> heightMapVector;

    irr::u32 imageWidth = heightMap->getDimension().Width;
    irr::u32 imageHeight = heightMap->getDimension().Height;

    //Find nearest 2^n+1 square size, upscaling if needed. 
    //Subtract 1 and find next power of 2, and add one (we need a size that is (2^n)+1)
    irr::s32 scaledWidth = (irr::s32)imageWidth-1;
    irr::s32 scaledHeight = (irr::s32)imageHeight-1;

    scaledWidth = pow(2.0,ceil(log2(scaledWidth))) + 1;
    scaledHeight = pow(2.0,ceil(log2(scaledHeight))) + 1;
    //find largest, returned vector will be square
    irr::u32 scaledSize = std::max(scaledWidth,scaledHeight);

    irr::f32 heightValue;

    for (unsigned int j=0; j<scaledSize; j++) {
        std::vector<irr::f32> heightMapLine;
        for (unsigned int k=0; k<scaledSize; k++) {
            
            //Pick the pixel to use (very simple scaling, to be replaced with bilinear interpolation)
            irr::f32 pixelX_float = (irr::f32)j * (irr::f32)imageWidth/(irr::f32)scaledSize;
            irr::f32 pixelY_float = (irr::f32)(scaledSize - k - 1) * (irr::f32)imageHeight/(irr::f32)scaledSize;
            irr::u32 pixelX_int = round(pixelX_float);
            irr::u32 pixelY_int = round(pixelY_float);

            irr::video::SColor pixelColor = heightMap->getPixel(pixelX_int,pixelY_int);
            
            if (usesRGBEncoding) {
                //Absolute height is (red * 256 + green + blue / 256) - 32768
                heightValue = ((irr::f32)pixelColor.getRed()*256 + (irr::f32)pixelColor.getGreen() + (irr::f32)pixelColor.getBlue()/256.0)-32768.0;
            } else {
                heightValue = pixelColor.getLightness();
            }

            heightMapLine.push_back(heightValue);
        }
        heightMapVector.push_back(heightMapLine);
    }

    return heightMapVector;

}

void Terrain::addRadarReflectingTerrain(std::vector<std::vector<irr::f32>> heightVector, irr::f32 positionX, irr::f32 positionZ, irr::f32 widthX, irr::f32 widthZ)
{
    //Add a terrain to be used to give the impression of a radar reflection from a land object.
    
    irr::scene::BCTerrainSceneNode* terrain = new irr::scene::BCTerrainSceneNode(
            dev,
            dev->getSceneManager()->getRootSceneNode(), 
            dev->getSceneManager(),
			dev->getSceneManager()->getFileSystem(), -1, 5, irr::scene::ETPS_33
        );

    bool loaded = terrain->loadHeightMapVector(heightVector,irr::video::SColor(255, 255, 255, 255), 0);

    if (!loaded) {
        //Could not load terrain
        std::cerr << "Could not load radar reflecting terrain." << std::endl;
        return;
    }

    irr::f32 scaleX = widthX/(terrain->getBoundingBox().MaxEdge.X - terrain->getBoundingBox().MinEdge.X);
    irr::f32 scaleZ = widthZ/(terrain->getBoundingBox().MaxEdge.Z - terrain->getBoundingBox().MinEdge.Z);

    terrain->setScale(irr::core::vector3df(scaleX,1.0f,scaleZ));
    terrain->setPosition(irr::core::vector3df(positionX, 0.f, positionZ));
    
    terrain->getMesh()->getMeshBuffer(0)->getMaterial().setFlag(irr::video::EMF_WIREFRAME, true);

    terrains.push_back(terrain);
}

irr::f32 Terrain::getHeight(irr::f32 x, irr::f32 z) const //Get height from global coordinates
{
    //Fallback minimum value
    irr::f32 terrainHeight = -FLT_MAX;
    
    //Check down list, find highest return value
    for (int i=(int)terrains.size()-1; i>=0; i--) {
        irr::f32 thisHeight = terrains.at(i)->getHeight(x,z);
        if (thisHeight > terrainHeight) {
            terrainHeight = thisHeight;
        }
    }

    return terrainHeight;
}

irr::f32 Terrain::longToX(irr::f32 longitude) const
{
    return ((longitude - primeTerrainLong ) * (primeTerrainXWidth)) / primeTerrainLongExtent;
}

irr::f32 Terrain::latToZ(irr::f32 latitude) const
{
    return ((latitude - primeTerrainLat ) * (primeTerrainZWidth)) / primeTerrainLatExtent;
}

irr::f32 Terrain::xToLong(irr::f32 x) const{
    return primeTerrainLong + x*primeTerrainLongExtent/primeTerrainXWidth;
}

irr::f32 Terrain::zToLat(irr::f32 z) const{
    return primeTerrainLat + z*primeTerrainLatExtent/primeTerrainZWidth;
}

void Terrain::moveNode(irr::f32 deltaX, irr::f32 deltaY, irr::f32 deltaZ)
{
    for (unsigned int i=0; i<terrains.size(); i++) {
        irr::core::vector3df currentPos = terrains.at(i)->getPosition();
        irr::f32 newPosX = currentPos.X + deltaX;
        irr::f32 newPosY = currentPos.Y + deltaY;
        irr::f32 newPosZ = currentPos.Z + deltaZ;
        terrains.at(i)->setPosition(irr::core::vector3df(newPosX,newPosY,newPosZ));
    }
}
