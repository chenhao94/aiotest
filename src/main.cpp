#include <iostream>
#include <fstream>
#include <string>

#include "tai.hpp"

int main()
{
    using namespace std;
    using namespace tai;

    BTreeDefault bt("tmp");
    Controller ctrl(1 << 28, 1 << 30);

    return 0;
}
