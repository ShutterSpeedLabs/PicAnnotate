#include "yaml_cpp.h"
#include "qdebug.h"
#include "qlogging.h"
#include <filesystem>
#include <regex>
#include <yaml-cpp/yaml.h>


YoloConfig readYoloConfig(const std::string& filename) {
    YoloConfig config;

    if (!std::filesystem::exists(filename)) {
        throw std::runtime_error("YAML file not found: " + filename);
    }

    try {
        YAML::Node root = YAML::LoadFile(filename);

        std::string basePath = std::filesystem::path(filename).parent_path().string();

        config.trainFolder = root["train"].as<std::string>();
        config.testFolder = root["test"].as<std::string>();
        config.valFolder = root["val"].as<std::string>();
        config.numClasses = root["nc"].as<int>();

        // Handle relative paths
        auto resolvePath = [&basePath](const std::string& path) {
            std::string cleanPath = path;
            if (cleanPath.substr(0, 2) == "./") {
                cleanPath = cleanPath.substr(2);
            }
        else if (cleanPath.substr(0, 3) == "../") {
            cleanPath = cleanPath.substr(3);
        }
            return std::filesystem::path(basePath) / cleanPath;
        };

        config.trainImagesPath = resolvePath(config.trainFolder).string();
        config.trainLabelsPath = resolvePath(config.trainFolder).string();
        config.trainLabelsPath = std::regex_replace(config.trainLabelsPath, std::regex("\\bimages\\b"), "labels");

        config.testImagesPath = resolvePath(config.testFolder).string();
        config.testLabelsPath = resolvePath(config.testFolder).string();
        config.testLabelsPath = std::regex_replace(config.testLabelsPath, std::regex("\\bimages\\b"), "labels");

        config.valImagesPath = resolvePath(config.valFolder).string();
        config.valLabelsPath = resolvePath(config.valFolder).string();
        config.valLabelsPath = std::regex_replace(config.valLabelsPath, std::regex("\\bimages\\b"), "labels");


        qDebug() << "Train folder:" << config.trainImagesPath;
        qDebug() << "Test folder:" <<  config.testImagesPath ;


        const YAML::Node& classNamesNode = root["names"];
        for (const auto& className : classNamesNode) {
            config.classNames.push_back(className.as<std::string>());
        }
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Error parsing YAML: " + std::string(e.what()));
    }

    return config;
}
