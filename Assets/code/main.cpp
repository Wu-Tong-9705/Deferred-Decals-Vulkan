#include "stdafx.h"

int main(int argc, char* argv[])
{
    Engine::Instance()->run();

#ifdef _DEBUG
    {
        Engine::Instance().reset();
        ObjectTracker::get()->check_for_leaks();
    }
#endif

    return 0;
}