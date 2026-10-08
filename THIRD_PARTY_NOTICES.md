# Third-party runtime notices

The application links dynamically to Qt 6.11.1 (Qt Core/Gui/Widgets/OpenGL/OpenGLWidgets and deployed plugins), supplied by MSYS2. Qt source and licensing: https://code.qt.io/cgit/qt/qtbase.git/ and https://www.qt.io/licensing/. Qt's open-source license texts, including LGPL v3 and GPL v3, are included in `third-party-licenses/qt6-base`. Libraries may be replaced with compatible builds; the application source and CMake build configuration accompany this delivery.

Additional runtime dependencies come from MSYS2 MinGW64 packages. Corresponding installed license directories are copied into `third-party-licenses` by the packaging script. MSYS2 package/source metadata: https://packages.msys2.org/. GCC runtime source and runtime exception: https://gcc.gnu.org/ and the included gcc-libs license texts.

This project does not embed or redistribute GRBL/grblHAL firmware or a virtual COM kernel driver. Protocol references are documentation only. Application-specific source remains the user's project; no license for those files is selected by this notice.
