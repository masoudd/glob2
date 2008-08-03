#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stack>
#include <vector>

struct Thunk;
struct Value;
struct Usl;

struct Thread
{
	struct Frame
	{
		typedef std::vector<Value*> Stack;
	
		Thunk* thunk;
		Stack stack;
		size_t nextInstr;
		
		Frame(Thunk* thunk):
			thunk(thunk)
		{
			nextInstr = 0;
		}
		
		void markForGC();
	};
	
	typedef std::vector<Frame> Frames;
	
	Usl* usl;
	Frames frames;
	
	Thread(Usl* usl):
		usl(usl)
	{}
	
	Value* run();
	Value* run(size_t& steps);
	Value* step();
	
	void markForGC();
};

#endif // ndef INTERPRETER_H
