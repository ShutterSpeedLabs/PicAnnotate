#include "yaml_cpp.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

YoloConfig readYoloConfig(const std::string& filename) {
    YoloConfig config;

    try {
        YAML::Node root = YAML::LoadFile(filename);

        config.trainFolder = root["train"].as<std::string>();
        config.testFolder = root["test"].as<std::string>();
        config.valFolder = root["val"].as<std::string>();
        config.numClasses = root["nc"].as<int>();

        const YAML::Node& classNamesNode = root["names"];
        for (const auto& className : classNamesNode) {
            config.classNames.push_back(className.as<std::string>());
        }
    } catch (const YAML::Exception& e) {
        std::cerr << "Error parsing YAML: " << e.what() << std::endl;
    }

    return config;
}
