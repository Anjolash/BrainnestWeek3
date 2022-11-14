#include <cstdlib>
#include <cstring>
#include <iostream>

[[noreturn]] void
exit_err_strerror(int errcode)
{
    std::cerr << ": " << std::strerror(errcode) << '\n';
    std::exit(EXIT_FAILURE);
}
