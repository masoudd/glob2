/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef MapScriptUSL_h
#define MapScriptUSL_h

#include "usl.h"
#include "interpreter.h"
#include "MapScriptError.h"
#include "SDL.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

class GameGUI;

///This represents a USL based map script
class MapScriptUSL
{
public:
	///Construct a map script
	MapScriptUSL(GameGUI* gui);
	
	///Destruct a map script
	~MapScriptUSL();
	
	///Construct all the global (from USL POV) values that reference glob2 objects
	void addGlob2Values(GameGUI* gui);

	///Encodes this MapScript into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this MapScript from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///This compiles the code, returns false on failure
	bool compileCode(const std::string& code);
	
	///This returns the error of the most recent compile
	const MapScriptError& getError() const;
	
	///Execute a step of script corresponding to a step of the game engine
	void syncStep(GameGUI *gui);
	
private:
	
	Usl usl;
	MapScriptError error;
};



#endif
