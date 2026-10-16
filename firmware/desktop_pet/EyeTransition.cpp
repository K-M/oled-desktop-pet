/***************************************************
Copyright (c) 2020 Luis Llamas
(www.luisllamas.es)

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses
****************************************************/

#include "EyeTransition.h"

// Modified for the desktop pet: the original blended Origin toward Destin in place
// every frame (Origin = Origin*(1-t) + Destin*t) with int16 truncation, which made
// transition speed depend on frame rate and caused stall-then-snap motion. This
// version interpolates from a fixed Start snapshot with smoothstep easing, so a
// transition always takes exactly Animation.Interval ms and moves monotonically.

EyeTransition::EyeTransition() : Animation(500) {
	Start = EyeConfig{};
	Destin = EyeConfig{};
}

void EyeTransition::Begin() {
	Start = *Origin;
	Animation.Restart();
}

void EyeTransition::Update() {
	float t = Animation.GetValue();          // linear 0..1 over Interval
	Apply(t * t * (3.0f - 2.0f * t));        // smoothstep ease-in-out
}

static inline int16_t lerpI(int16_t a, int16_t b, float e) {
	return (int16_t)lroundf(a + (b - a) * e);
}

void EyeTransition::Apply(float e) {
	Origin->OffsetX = lerpI(Start.OffsetX, Destin.OffsetX, e);
	Origin->OffsetY = lerpI(Start.OffsetY, Destin.OffsetY, e);
	Origin->Height = lerpI(Start.Height, Destin.Height, e);
	Origin->Width = lerpI(Start.Width, Destin.Width, e);
	Origin->Slope_Top = Start.Slope_Top + (Destin.Slope_Top - Start.Slope_Top) * e;
	Origin->Slope_Bottom = Start.Slope_Bottom + (Destin.Slope_Bottom - Start.Slope_Bottom) * e;
	Origin->Radius_Top = lerpI(Start.Radius_Top, Destin.Radius_Top, e);
	Origin->Radius_Bottom = lerpI(Start.Radius_Bottom, Destin.Radius_Bottom, e);
	Origin->Inverse_Radius_Top = lerpI(Start.Inverse_Radius_Top, Destin.Inverse_Radius_Top, e);
	Origin->Inverse_Radius_Bottom = lerpI(Start.Inverse_Radius_Bottom, Destin.Inverse_Radius_Bottom, e);
	Origin->Inverse_Offset_Top = lerpI(Start.Inverse_Offset_Top, Destin.Inverse_Offset_Top, e);
	Origin->Inverse_Offset_Bottom = lerpI(Start.Inverse_Offset_Bottom, Destin.Inverse_Offset_Bottom, e);
}
