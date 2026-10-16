/***************************************************
Copyright (c) 2020 Luis Llamas
(www.luisllamas.es)

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version. 

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses 
****************************************************/

#ifndef _EYETRANSITION_h
#define _EYETRANSITION_h

#include <Arduino.h>
#include "Animations.h"
#include "EyeConfig.h"

class EyeTransition {
public:
	EyeTransition();

	EyeConfig* Origin;   // the eye's live config, written every frame
	EyeConfig Start;     // snapshot of Origin when the transition began
	EyeConfig Destin;    // target preset

	void Begin();        // snapshot Start = *Origin and restart the ramp

	RampAnimation Animation;

	void Update();
	void Apply(float t);
};

#endif

