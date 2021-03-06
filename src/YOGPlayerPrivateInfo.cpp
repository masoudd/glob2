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
#include <assert.h>

#include "YOGPlayerPrivateInfo.h"
#include "SDL_net.h"
#include "Stream.h"

YOGPlayerPrivateInfo::YOGPlayerPrivateInfo()
{

}


void YOGPlayerPrivateInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerPrivateInfo");
	stream->writeLeaveSection();
}



void YOGPlayerPrivateInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGPlayerPrivateInfo");
	stream->readLeaveSection();
}



bool YOGPlayerPrivateInfo::operator==(const YOGPlayerPrivateInfo& rhs) const
{
	//TODO: what's the point of this? -Wall found it
	assert(false);
}



bool YOGPlayerPrivateInfo::operator!=(const YOGPlayerPrivateInfo& rhs) const
{
	assert(false);
}

