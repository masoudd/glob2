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

#ifndef __GUILIST_H
#define __GUILIST_H

#include "GUIBase.h"
#include <vector>
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	//! A widget that display a list of strings.
	class List: public HighlightableWidget
	{
	protected:
		//! the name of the used font
		std::string font;
		//! the list of strings
		std::vector<std::string> strings;
		//! the current selected entry. -1 means no selection
		Sint32 nth;
		//! index of the first entry to be shown, used for scrolling
		size_t disp;
	
		//! Cached variables, do not serialise, reconstructed on paint() call
		//! Length of the scroll box, this is a cache
		unsigned blockLength, blockPos, textHeight;
		//! Pointer to font, this is a cache
		GAGCore::Font *fontPtr;
	
	public:
		//! Creator
		List() { fontPtr = NULL; }
		//! Creator, with arguments. x, y, w, h are the positional information. hAlign and vAlign the layouting flags. font the name of the font to use
		List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font);
		//! Destructor
		virtual ~List();
	
		virtual void onTimer(Uint32 tick) { }
		virtual void onSDLEvent(SDL_Event *event);
		virtual void init(void);
		virtual void paint(void);
	
		//! Add text to pos in the list
		void addText(const std::string &text, size_t pos);
		//! Add text to the end of the list
		void addText(const std::string &text);
		//! Remove text at pos in the list
		void removeText(size_t pos);
		//! Returns true if text is in the list
		bool isText(const std::string &text) const;
		//! Removes all the content of the list
		void clear(void);
		//! Returns the value of pos. If pos is out of bounds, returns ""
		const std::string &getText(size_t pos) const;
		//! Returns the value of the current selection. If no selection (mtf == -1), returns "
		const std::string &get(void) const;
		//! Sorts the list (override it if you don't like it)
		virtual void sort(void);
	
		//! Called when selection changes (default: signal parent)
		virtual void selectionChanged();
	
		//! Return the index of the current selection. Returns -1 if no selection
		int getSelectionIndex(void) const;
		//! Set the index of the current selection. Set -1 for no selection
		void setSelectionIndex(int index);
	};
}

#endif

