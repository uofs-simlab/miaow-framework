#ifndef MIAOW_HPP
#define MIAOW_HPP

#include <string>

/**
 * Run a minimal CAF request/reply interaction and return the greeting text.
 * Used by the example to demonstrate CAF integration without exposing CAF types.
 * @return greeting text, or an error string if the request fails.
 */
std::string miaow_actor_hello();

/**
 * Initialize MIAOW's CAF global state.
 * Safe to call multiple times; recommended to call once at startup.
 */
void miaow_init();

#endif /* MIAOW_HPP */
