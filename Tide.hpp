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

#ifndef __TIDE_HPP_INCLUDED__
#define __TIDE_HPP_INCLUDED__

#include "irrlicht.h"

#include <vector>
#include <string>
#include <stdint.h> //for uint64_t

class Tide {

struct tidalHarmonic {
    irr::f32 amplitude; //Metres
    irr::f32 offset; //Offset in degrees (Relative to peak at 0000 on 1 Jan 1970)
    irr::f32 speed; //Degrees per hour

    //Default constructor - initialise to zero
    tidalHarmonic():
        amplitude(0),offset(0),speed(0){}
};

public:
    Tide();
    virtual ~Tide();
    void load(const std::string& worldName);
    void update(uint64_t absoluteTime);
    irr::f32 getTideHeight(); //To be called after update(time)
    irr::core::vector2df getTidalStream(irr::f32 posX, irr::f32 posZ, uint64_t absoluteTime) const; //Does not need update() to be called before this

private:
    irr::f32 tideHeight;
    std::vector<tidalHarmonic> tidalHarmonics;


};

#endif // __TIDE_HPP_INCLUDED__

