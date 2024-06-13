#ifndef YAML_CPP_H
#define YAML_CPP_H

#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>


struct YoloConfig {
    std::string trainFolder;
    std::string testFolder;
    std::string valFolder;
    int numClasses;
    std::vector<std::string> classNames;
};

YoloConfig readYoloConfig(const std::string& filename);


#endif // YAML_CPP_H
