//
// Created by mando on 12/01/21.
//

#include <boost/program_options.hpp>
#include <iostream>
#include "SystemContext.h"
#include "Topic.h"
#include <string>
namespace po = boost::program_options;

int main(int argc, char** argv) {
    SystemContext &systemContext = SystemContext::GenerateContext(argc, argv);

    return 0;
}