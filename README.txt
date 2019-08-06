/////////////////////////////////////////////////////////////////////////////
// car.cpp
// Dennis Brown
// 5/22/95
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// DESCRIPTION:
// ------------
// This is a simple "game" in which a player drives a car around a
// multi-screen scrolling background (scrolling in both the X and Y
// directions). The perspective is overhead. The controls are:
// UP - accelerate; DOWN - decelerate; LEFT - turn counter-clockwise;
// RIGHT - turn clockwise; ESC - quit.
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// FEATURES:
// ---------
// * uses extended memory to hold the background picture cells currently
//   not in use (3rd-party freely-distributable package used for HIMEM access)
// * uses double-buffering for flicker-free animation
// * uses a sprite data type for the player which can handle animated sprites
// * uses its own keyboard interrupt handler to handle multiple keypresses
//   and to implement responsive controls
/////////////////////////////////////////////////////////////////////////////
