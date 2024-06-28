# PicAnnotate: YOLO Dataset Viewer and Annotator

PicAnnotate is a Qt-based application for viewing and annotating datasets in YOLO format. It allows users to load YOLO datasets, view images with their corresponding bounding box annotations, and navigate through the dataset.

## Features

- Load YOLO datasets using YAML configuration files
- Display images with bounding box annotations
- Navigate through dataset images using Next and Previous buttons
- Zoom in and out of images while maintaining annotation visibility
- Select images from a list view
- Display different classes with unique color-coded bounding boxes

## Requirements

- Qt 5.x or later
- OpenCV (for image processing)
- yaml-cpp (for parsing YAML files)

## Building the Project

1. Ensure you have Qt, OpenCV, and yaml-cpp installed on your system.
2. Clone this repository:
   git clone https://github.com/ShutterSpeedLabs/PicAnnotate.git

3. Open the project in Qt Creator or build it using qmake:
   cd PicAnnotate
   qmake
   make

## Usage

1. Run the PicAnnotate application.
2. Click on "File" -> "Read YAML" to load a YOLO dataset.
3. Select the YAML configuration file for your dataset.
4. Choose which subset of the dataset to load (train, test, or validation).
5. Use the Next and Previous buttons to navigate through images.
6. Use the Zoom In and Zoom Out buttons to adjust the image view.
7. Click on image names in the list view to jump to specific images.

## Code Structure

- `picannotate.h` and `picannotate.cpp`: Main application window and logic
- `yaml_cpp.h` and `yaml_cpp.cpp`: YAML configuration file parsing
- `datasetfn.h` and `datasetfn.cpp`: Dataset loading functions
- `globalVar.h`: Global variables and structures

## Contributing

Contributions to PicAnnotate are welcome! Please feel free to submit pull requests, create issues or spread the word.

## License

MIT License

## Acknowledgements

This project uses the following open-source libraries:
- Qt
- OpenCV
- yaml-cpp

## Contact

Puran Dhakrey - shutterspeedlabs@gmail.com

Project Link: [https://github.com/yourusername/PicAnnotate](https://github.com/ShutterSpeedLabs/PicAnnotate.git)
