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

#ifndef __MAIN_MENU_SCREEN_H
#define __MAIN_MENU_SCREEN_H

#include <GUIBase.h>
namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
using namespace GAGGUI;

class MainMenuScreen:public Screen
{
public:
	enum
	{
		CAMPAIGN,
		TUTORIAL,
		LOAD_GAME,
		CUSTOM,
		MULTIPLAYERS_YOG,
		MULTIPLAYERS_LAN,
		GAME_SETUP,
		EDITOR,
		CREDITS,
		QUIT,
	};
private:
	DrawableSurface *background;
public:
	MainMenuScreen();
	virtual ~MainMenuScreen();
	void onAction(Widget *source, Action action, int par1, int par2);
	static int menu(void);
};

#endif
