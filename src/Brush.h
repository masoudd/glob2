/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __BRUSH_H
#define __BRUSH_H

#include <vector>

//! A click of the brush tool to the map
struct BrushApplication
{
	BrushApplication(int x, int y, int figure) { this->x=x; this->y=y; this->figure=figure; }
	int x;
	int y;
	int figure;
};

//! A brush tool is the GUI and the settings container for a brush's operations
class BrushTool
{
public:
	enum Mode
	{
		MODE_NONE = 0,
		MODE_ADD,
		MODE_DEL
	};
	
protected:
	unsigned figure;
	Mode mode;
	
public:
	BrushTool();
	//! Draw the brush tool and its actual state at a given coordinate 
	void draw(int x, int y);
	//! Handle the click for giben coordinate. Select correct mode and figure, accept negative coordinates for y
	void handleClick(int x, int y);
	//! Deselect any brush
	void unselect(void) { mode = MODE_NONE; }
	//! Draw the actual brush (not the brush tool)
	void drawBrush(int x, int y);
	//! Return the mode of the brush
	unsigned getType(void) { return static_cast<unsigned>(mode); }
	//! Return the id of the actual figure
	unsigned getFigure(void) { return figure; }
	
	//! Return the full width of a brush
	static int getBrushWidth(unsigned figure);
	//! Return the full height of a brush
	static int getBrushHeight(unsigned figure);
	//! Return the half width minus 1 of a brush (its "ray" in x)
	static int getBrushDimX(unsigned figure);
	//! Return the half height minus 1 of a brush (its "ray" in y)
	static int getBrushDimY(unsigned figure);
	//! Return the value of a pixel of a given brush
	static bool getBrushValue(unsigned figure, int x, int y);
};

namespace Utilities
{
	class BitArray;
}

class Map;

//! Accumulator that can store brush and return the resulting bitmap
class BrushAccumulator
{
public:
	//! Dimension of the resulting bitmap
	struct AreaDimensions
	{
		int centerX, centerY;
		int minX, minY, maxX, maxY;
	};
	
	// FIXME : handle wrap !!!!!!!!!!
	
protected:
	//! The list of brush applications
	std::vector<BrushApplication> applications;
	//! The actual dimensions of the resulting applications
	AreaDimensions dim;
	
public:
	//! Apply this brush to the brush application vector and extend dim as required
	void applyBrush(const Map *map, const BrushApplication &brush);
	//! Clear the vector of brush applications
	void clear(void) { applications.clear(); }
	//! Return a bitmap which is the result of the fusion of all accumulated brush applications
	bool getBitmap(Utilities::BitArray *array, AreaDimensions *dim);
	//! Return the area surface
	unsigned getAreaSurface(void);
	//! Return the number of brush applied
	size_t getApplicationCount(void) { return applications.size(); }
};

#endif
