#include <iostream>
#include <fstream>
#include <string>

#include "tai.hpp"

int main()
{
    using namespace std;
    using namespace tai;

    cout << "Creating B-tree..." << endl;
    BTreeDefault bt("tmp");
    cout << "Creating Controller..." << endl;
    Controller ctrl(1 << 28, 1 << 30);

    cout << "Complete!" << endl;
    return 0;
}
