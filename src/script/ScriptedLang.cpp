/*
 * Copyright 2011-2012 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "script/ScriptedLang.h"

#include "ai/Paths.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Inventory.h"
#include "graphics/Math.h"
#include "scene/Interactive.h"
#include "script/ScriptEvent.h"
#include "script/ScriptUtils.h"

using std::string;

extern SCRIPT_EVENT AS_EVENT[];

namespace script {

namespace {

class NopCommand : public Command {
	
public:
	
	NopCommand() : Command("nop") { }
	
	Result execute(Context & context) {
		
		ARX_UNUSED(context);
		
		DebugScript("");
		
		return Success;
	}
	
};

class GotoCommand : public Command {
	
	bool sub;
	
public:
	
	GotoCommand(string command, bool _sub = false) : Command(command), sub(_sub) { }
	
	Result execute(Context & context) {
		
		string label = context.getWord();
		
		DebugScript(' ' << label);
		
		if(!sub) {
			size_t pos = context.skipCommand();
			if(pos != (size_t)-1) {
				ScriptWarning << "unexpected text at " << pos;
			}
		}
		
		if(!context.jumpToLabel(label, sub)) {
			ScriptError << "unknown label \"" << label << '"';
			return AbortError;
		}
		
		return Jumped;
	}
	
};

class AbortCommand : public Command {
	
	Result result;
	
public:
	
	AbortCommand(string command, Result _result) : Command(command), result(_result) { }
	
	Result execute(Context & context) {
		
		ARX_UNUSED(context);
		
		DebugScript("");
		
		return result;
	}
	
};

class RandomCommand : public Command {
	
public:
	
	RandomCommand() : Command("random") { }
	
	Result execute(Context & context) {
		
		float chance = clamp(context.getFloat(), 0.f, 100.f);
		
		DebugScript(' ' << chance);
		
		float t = rnd() * 100.f;
		if(chance < t) {
			context.skipStatement();
		}
		
		return Success;
	}
	
};

class ReturnCommand : public Command {
	
public:
	
	ReturnCommand() : Command("return") { }
	
	Result execute(Context & context) {
		
		DebugScript("");
		
		if(!context.returnToCaller()) {
			ScriptError << "return failed";
			return AbortError;
		}
		
		return Success;
	}
	
};

class SetMainEventCommand : public Command {
	
public:
	
	explicit SetMainEventCommand(const string & command) : Command(command, AnyEntity) { }
	
	Result execute(Context & context) {
		
		string event = context.getWord();
		
		DebugScript(' ' << event);
		
		ARX_SCRIPT_SetMainEvent(context.getEntity(), event);
		
		return Success;
	}
	
};

class StartStopTimerCommand : public Command {
	
	const bool start;
	
public:
	
	StartStopTimerCommand(const string & command, bool _start) : Command(command), start(_start) { }
	
	Result execute(Context & context) {
		
		string timer = context.getWord();
		
		DebugScript(' ' << timer);
		
		long t;
		if(timer == "timer1") {
			t = 0;
		} else if(timer == "timer2") {
			t = 1;
		} else if(timer == "timer3") {
			t = 2;
		} else if(timer == "timer4") {
			t = 3;
		} else {
			ScriptWarning << "invalid timer: " << timer;
			return Failed;
		}
		
		EERIE_SCRIPT * script = context.getMaster();
		if(start) {
			script->timers[t] = (unsigned long)(arxtime);
			if(script->timers[t] == 0) {
				script->timers[t] = 1;
			}
		} else {
			script->timers[t] = 0;
		}
		
		return Success;
	}
	
};

class SendEventCommand : public Command {
	
	enum SendTarget {
		SEND_NPC = 1,
		SEND_ITEM = 2,
		SEND_FIX = 4
	};
	DECLARE_FLAGS(SendTarget, SendTargets)
	
public:
	
	SendEventCommand() : Command("sendevent") { }
	
	Result execute(Context & context) {
		
		SendTargets sendto = 0;
		bool radius = false;
		bool zone = false;
		bool group = false;
		HandleFlags("gfinrz") {
			group = test_flag(flg, 'g');
			sendto |= (flg & flag('f')) ? SEND_FIX : (SendTargets)0;
			sendto |= (flg & flag('i')) ? SEND_ITEM : (SendTargets)0;
			sendto |= (flg & flag('n')) ? SEND_NPC : (SendTargets)0;
			radius = test_flag(flg, 'r');
			zone = test_flag(flg, 'z');
		}
		if(!sendto) {
			sendto = SEND_NPC;
		}
		
		string groupname;
		if(group) {
			groupname = context.getStringVar(context.getWord());
		}
		
		string event = context.getWord();
		
		string zonename;
		if(zone) {
			zonename = context.getStringVar(context.getWord());
		}
		
		float rad = 0.f;
		if(radius) {
			rad = context.getFloat();
		}
		
		string target;
		if(!group && !zone && !radius) {
			target = context.getStringVar(context.getWord());
			
			// TODO(broken-scripts) work around broken scripts 
			for(size_t i = 0; i < SM_MAXCMD; i++) {
				if(target == AS_EVENT[i].name.c_str() + 3) {
					std::swap(target, event);
					break;
				}
			}
		}
		
		string params = context.getWord();
		
		if(radius) {
			DebugScript(' ' << event << (params.empty() ? "" : " \"" + params + '"') << " to " << (group ? "group " + groupname : "everyone") << " in radius " << rad);
		} else if(zone) {
			DebugScript(' ' << event << (params.empty() ? "" : " \"" + params + '"') << " to " << (group ? "group " + groupname : "everyone") << " in zone " << zonename);
		} else {
			DebugScript(' ' << event << (params.empty() ? "" : " \"" + params + '"') << " to " << target);
		}
		
		Entity * oes = EVENT_SENDER;
		EVENT_SENDER = context.getEntity();
		
		Entity * io = context.getEntity();
		
		if(radius) { // SEND EVENT TO ALL OBJECTS IN A RADIUS
			
			for(size_t l = 0 ; l < entities.size() ; l++) {
				
				if(!entities[l] || entities[l] == io
				   || (entities[l]->ioflags & (IO_CAMERA|IO_MARKER))) {
					continue;
				}
				
				if(group && entities[l]->groups.find(groupname) == entities[l]->groups.end()) {
					continue;
				}
				
				if(((sendto & SEND_NPC) && (entities[l]->ioflags & IO_NPC))
				   || ((sendto & SEND_FIX) && (entities[l]->ioflags & IO_FIX))
				   || ((sendto & SEND_ITEM) && (entities[l]->ioflags & IO_ITEM))) {
					
					Vec3f _pos, _pos2;
					GetItemWorldPosition(entities[l], &_pos);
					GetItemWorldPosition(io, &_pos2);
					
					if(distSqr(_pos, _pos2) <= square(rad)) {
						io->stat_sent++;
						Stack_SendIOScriptEvent(entities[l], SM_NULL, params, event);
					}
				}
			}
			
		} else if(zone) { // SEND EVENT TO ALL OBJECTS IN A ZONE
			
			ARX_PATH * ap = ARX_PATH_GetAddressByName(zonename);
			if(!ap) {
				ScriptWarning << "unknown zone: " << zonename;
				EVENT_SENDER = oes;
				return Failed;
			}
			
			for(size_t l = 0; l < entities.size(); l++) {
				
				if(!entities[l] || (entities[l]->ioflags & (IO_CAMERA|IO_MARKER))) {
					continue;
				}
				
				if(group && entities[l]->groups.find(groupname) == entities[l]->groups.end()) {
					continue;
				}
				
				if(((sendto & SEND_NPC) && (entities[l]->ioflags & IO_NPC))
				   || ((sendto & SEND_FIX) && (entities[l]->ioflags & IO_FIX))
				   || ((sendto & SEND_ITEM) && (entities[l]->ioflags & IO_ITEM))) {
					
					Vec3f _pos;
					GetItemWorldPosition(entities[l], &_pos);
					
					if(ARX_PATH_IsPosInZone(ap, _pos.x, _pos.y, _pos.z)) {
						io->stat_sent++;
						Stack_SendIOScriptEvent(entities[l], SM_NULL, params, event);
					}
				}
			}
			
		} else if(group) { // sends an event to all members of a group
			
			for(size_t l = 0; l < entities.size(); l++) {
				
				if(!entities[l] || entities[l] == io) {
					continue;
				}
				
				if(entities[l]->groups.find(groupname) == entities[l]->groups.end()) {
					continue;
				}
				
				io->stat_sent++;
				Stack_SendIOScriptEvent(entities[l], SM_NULL, params, event);
			}
			
		} else { // single object event
			
			Entity * t = entities.getById(target, io);
			if(!t) {
				EVENT_SENDER = oes;
				return Failed;
			}
			
			io->stat_sent++;
			Stack_SendIOScriptEvent(t, SM_NULL, params, event);
		}
		
		EVENT_SENDER = oes;
		
		return Success;
	}
	
};

class SetEventCommand : public Command {
	
	typedef std::map<string, DisabledEvent> Events;
	Events events;
	
public:
	
	SetEventCommand() : Command("setevent") {
		events["collide_npc"] = DISABLE_COLLIDE_NPC;
		events["chat"] = DISABLE_CHAT;
		events["hit"] = DISABLE_HIT;
		events["inventory2_open"] = DISABLE_INVENTORY2_OPEN;
		events["detectplayer"] = DISABLE_DETECT;
		events["hear"] = DISABLE_HEAR;
		events["aggression"] = DISABLE_AGGRESSION;
		events["main"] = DISABLE_MAIN;
		events["cursormode"] = DISABLE_CURSORMODE;
		events["explorationmode"] = DISABLE_EXPLORATIONMODE;
	}
	
	Result execute(Context & context) {
		
		string name = context.getWord();
		bool enable = context.getBool();
		
		DebugScript(' ' << name << ' ' << enable);
		
		Events::const_iterator it = events.find(name);
		if(it == events.end()) {
			ScriptWarning << "unknown event: " << name;
			return Failed;
		}
		
		if(enable) {
			context.getMaster()->allowevents &= ~it->second;
		} else {
			context.getMaster()->allowevents |= it->second;
		}
		
		return Success;
	}
	
};

class IfCommand : public Command {
	
	// TODO(script) move to context?
	static ValueType getVar(const Context & context, const string & var, string & s, float & f, ValueType def) {
		
		char c = (var.empty() ? '\0' : var[0]);
		
		EERIE_SCRIPT * es = context.getMaster();
		Entity * io = context.getEntity();
		
		switch(c) {
			
			case '^': {
				
				long l;
				switch(getSystemVar(es, io, var, s, &f, &l)) {
					
					case TYPE_TEXT: return TYPE_TEXT;
					
					case TYPE_FLOAT: return TYPE_FLOAT;
					
					case TYPE_LONG: {
						f = static_cast<float>(l);
						return TYPE_FLOAT;
					}
					
					default: {
						ARX_DEAD_CODE();
						return TYPE_TEXT;
					}
					
				}
			}
			
			case '#': {
				f = GETVarValueLong(svar, NB_GLOBALS, var);
				return TYPE_FLOAT;
			}
			
			case '\xA7': {
				f = GETVarValueLong(es->lvar, es->nblvar, var);
				return TYPE_FLOAT;
			}
			
			case '&': {
				f = GETVarValueFloat(svar, NB_GLOBALS, var);
				return TYPE_FLOAT;
			}
			
			case '@': {
				f = GETVarValueFloat(es->lvar, es->nblvar, var);
				return TYPE_FLOAT;
			}
			
			case '$': {
				s = GETVarValueText(svar, NB_GLOBALS, var);
				return TYPE_TEXT;
			}
			
			case '\xA3': {
				s = GETVarValueText(es->lvar, es->nblvar, var);
				return TYPE_TEXT;
			}
			
			default: {
				if(def == TYPE_TEXT) {
					s = var;
					return TYPE_TEXT;
				} else {
					f = static_cast<float>(atof(var.c_str()));
					return TYPE_FLOAT;
				}
			}
			
		}
		
	}
	
	class Operator {
		
		string name;
		ValueType type;
		
	public:
		
		Operator(const string & _name, ValueType _type) : name(_name), type(_type) { }
		
		virtual ~Operator() { }
		
		virtual bool number(const Context & context, float left, float right) {
			ARX_UNUSED(left), ARX_UNUSED(right);
			ScriptWarning << "operator " << name << " is not aplicable to numbers";
			return true;
		}
		
		virtual bool text(const Context & context, const string & left, const string & right) {
			ARX_UNUSED(left), ARX_UNUSED(right);
			ScriptWarning << "operator " << name << " is not aplicable to text";
			return false;
		}
		
		inline string getName() { return "if"; }
		inline const string & getOperator() { return name; }
		inline ValueType getType() { return type; }
		
	};
	
	typedef std::map<string, Operator *> Operators;
	Operators operators;
	
	void addOperator(Operator * op) {
		
		typedef std::pair<Operators::iterator, bool> Res;
		
		Res res = operators.insert(std::make_pair(op->getOperator(), op));
		
		if(!res.second) {
			LogError << "duplicate script 'if' operator name: " + op->getOperator();
			delete op;
		}
		
	}
	
	class IsElementOperator : public Operator {
		
	public:
		
		IsElementOperator() : Operator("iselement", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & seek, const string & text) {
			ARX_UNUSED(context);
			
			for(size_t pos = 0, next; next = text.find(' ', pos), true ; pos = next + 1) {
				
				if(next == string::npos) {
					return (text.compare(pos, text.length() - pos, seek) == 0);
				}
				
				if(text.compare(pos, next - pos, seek) == 0) {
					return true;
				}
				
			}
			
			return false; // for stupid compilers
		}
		
	};
	
	class IsClassOperator : public Operator {
		
	public:
		
		IsClassOperator() : Operator("isclass", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & left, const string & right) {
			ARX_UNUSED(context);
			return (left.find(right) != string::npos || right.find(left) != string::npos);
		}
		
	};
	
	class IsGroupOperator : public Operator {
		
	public:
		
		IsGroupOperator() : Operator("isgroup", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & obj, const string & group) {
			
			Entity * t = entities.getById(obj, context.getEntity());
			
			return (t != NULL && t->groups.find(group) != t->groups.end());
		}
		
	};
	
	class NotIsGroupOperator : public Operator {
		
	public:
		
		NotIsGroupOperator() : Operator("!isgroup", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & obj, const string & group) {
			
			Entity * t = entities.getById(obj, context.getEntity());
			
			return (t != NULL && t->groups.find(group) == t->groups.end());
		}
		
	};
	
	class IsTypeOperator : public Operator {
		
	public:
		
		IsTypeOperator() : Operator("istype", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & obj, const string & type) {
			
			Entity * t = entities.getById(obj, context.getEntity());
			
			ItemType flag = ARX_EQUIPMENT_GetObjectTypeFlag(type);
			if(!flag) {
				ScriptWarning << "unknown type: " << type;
				return false;
			}
			
			return (t != NULL && (t->type_flags & flag));
		}
		
	};
	
	class IsInOperator : public Operator {
		
	public:
		
		IsInOperator() : Operator("isin", TYPE_TEXT) { }
		
		bool text(const Context & context, const string & needle, const string & haystack) {
			return ARX_UNUSED(context), (haystack.find(needle) != string::npos);
		}
		
	};
	
	class EqualOperator : public Operator {
		
	public:
		
		EqualOperator() : Operator("==", TYPE_FLOAT) { }
		
		bool text(const Context & context, const string & left, const string & right) {
			return ARX_UNUSED(context), (left == right);
		}
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left == right);
		}
		
	};
	
	class NotEqualOperator : public Operator {
		
	public:
		
		NotEqualOperator() : Operator("!=", TYPE_FLOAT) { }
		
		bool text(const Context & context, const string & left, const string & right) {
			return ARX_UNUSED(context), (left != right);
		}
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left != right);
		}
		
	};
	
	class LessEqualOperator : public Operator {
		
	public:
		
		LessEqualOperator() : Operator("<=", TYPE_FLOAT) { }
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left <= right);
		}
		
	};
	
	class LessOperator : public Operator {
		
	public:
		
		LessOperator() : Operator("<", TYPE_FLOAT) { }
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left < right);
		}
		
	};
	
	class GreaterEqualOperator : public Operator {
		
	public:
		
		GreaterEqualOperator() : Operator(">=", TYPE_FLOAT) { }
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left >= right);
		}
		
	};
	
	class GreaterOperator : public Operator {
		
	public:
		
		GreaterOperator() : Operator(">", TYPE_FLOAT) { }
		
		bool number(const Context & context, float left, float right) {
			return ARX_UNUSED(context), (left > right);
		}
		
	};
	
public:
	
	IfCommand() : Command("if") {
		addOperator(new IsElementOperator);
		addOperator(new IsClassOperator);
		addOperator(new IsGroupOperator);
		addOperator(new NotIsGroupOperator);
		addOperator(new IsTypeOperator);
		addOperator(new IsInOperator);
		addOperator(new EqualOperator);
		addOperator(new NotEqualOperator);
		addOperator(new LessEqualOperator);
		addOperator(new LessOperator);
		addOperator(new GreaterEqualOperator);
		addOperator(new GreaterOperator);
	}
	
	Result execute(Context & context) {
		
		string left = context.getWord();
		
		string op = context.getWord();
		
		string right = context.getWord();
		
		Operators::const_iterator it = operators.find(op);
		if(it == operators.end()) {
			ScriptWarning << "unknown operator: " << op;
			return Failed;
		}
		
		float f1, f2;
		string s1, s2;
		ValueType t1 = getVar(context, left, s1, f1, it->second->getType());
		ValueType t2 = getVar(context, right, s2, f2, t1);
		
		if(t1 != t2) {
			ScriptWarning << "incompatible types: \"" << left << "\" (" << (t1 == TYPE_TEXT ? "text" : "number") << ") and \"" << right << "\" (" << (t2 == TYPE_TEXT ? "text" : "number") << ')';
			context.skipStatement();
			return Failed;
		}
		
		bool condition;
		if(t1 == TYPE_TEXT) {
			condition = it->second->text(context, s1, s2);
			DebugScript(" \"" << left << "\" " << op << " \"" << right << "\"  ->  \"" << s1 << "\" " << op << " \"" << s2 << "\"  ->  " << (condition ? "true" : "false")); // TODO fix formatting in Logger and use std::boolalpha
		} else {
			condition = it->second->number(context, f1, f2);
			DebugScript(" \"" << left << "\" " << op << " \"" << right << "\"  ->  " << f1 << " " << op << " " << f2 << "  ->  " << (condition ? "true" : "false"));
		}
		
		if(!condition) {
			context.skipStatement();
		}
		
		return Success;
	}
	
};

class ElseCommand : public Command {
	
public:
	
	ElseCommand() : Command("else") { }
	
	Result execute(Context & context) {
		
		DebugScript("");
		
		context.skipStatement();
		
		return Success;
	}
	
};

}

const string getName() {
	return "timer";
}

void timerCommand(const string & timer, Context & context) {
	
	// Checks if the timer is named by caller of if it needs a default name
	string timername = timer.empty() ? ARX_SCRIPT_Timer_GetDefaultName() : timer;
	
	bool mili = false, idle = false;
	HandleFlags("mi") {
		mili = test_flag(flg, 'm');
		idle = test_flag(flg, 'i');
	}
	
	string command = context.getWord();
	
	DebugScript(' ' << options << ' ' << command);
	
	Entity * io = context.getEntity();
	
	if(command == "kill_local") {
		DebugScript(' ' << options << " kill_local");
		ARX_SCRIPT_Timer_Clear_All_Locals_For_IO(io);
		return;
	}
	
	ARX_SCRIPT_Timer_Clear_By_Name_And_IO(timername, io);
	if(command == "off") {
		DebugScript(' ' << options << " off");
		return;
	}
	
	long count = (long)context.getFloatVar(command);
	long millisecons = (long)context.getFloat();
	
	if(!mili) {
		millisecons *= 1000;
	}
	
	size_t pos = context.skipCommand();
	
	long num = ARX_SCRIPT_Timer_GetFree();
	if(num == -1) {
		ScriptError << "no free timer available";
		return;
	}
	
	ActiveTimers++;
	scr_timer[num].es = context.getScript();
	scr_timer[num].exist = 1;
	scr_timer[num].io = io;
	scr_timer[num].msecs = millisecons;
	scr_timer[num].name = timername;
	scr_timer[num].pos = pos;
	scr_timer[num].tim = (unsigned long)(arxtime);
	scr_timer[num].times = count;
	
	scr_timer[num].flags = (idle && io) ? 1 : 0;
	
}

void setupScriptedLang() {
	
	ScriptEvent::registerCommand(new NopCommand); // TODO(script-parser) remove
	ScriptEvent::registerCommand(new GotoCommand("goto")); // TODO(script-parser) remove when possible
	ScriptEvent::registerCommand(new GotoCommand("gosub", true));
	ScriptEvent::registerCommand(new AbortCommand("accept", Command::AbortAccept));
	ScriptEvent::registerCommand(new AbortCommand("refuse", Command::AbortRefuse));
	ScriptEvent::registerCommand(new RandomCommand);
	ScriptEvent::registerCommand(new ReturnCommand);
	ScriptEvent::registerCommand(new SetMainEventCommand("setstatus"));
	ScriptEvent::registerCommand(new SetMainEventCommand("setmainevent"));
	ScriptEvent::registerCommand(new StartStopTimerCommand("starttimer", true));
	ScriptEvent::registerCommand(new StartStopTimerCommand("stoptimer", false));
	ScriptEvent::registerCommand(new SendEventCommand);
	ScriptEvent::registerCommand(new SetEventCommand);
	ScriptEvent::registerCommand(new IfCommand);
	ScriptEvent::registerCommand(new ElseCommand); // TODO(script-parser) remove
	
}

} // namespace script
