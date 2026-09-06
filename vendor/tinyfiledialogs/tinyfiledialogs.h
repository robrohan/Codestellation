/*_________
 /         \ tinyfiledialogs.h v2.9.3 [July 12, 2017] zlib licence
 |tiny file| Unique header file created [November 9, 2014]
 | dialogs | Copyright (c) 2014 - 2017 Guillaume Vareille http://ysengrin.com
 \____  ___/ http://tinyfiledialogs.sourceforge.net
      \|

A cross-platform file dialog library (Windows / macOS / Linux).
Add tinyfiledialogs.c to your build and include this header.
On Windows, link against Comdlg32.lib and Ole32.lib.
*/

#ifndef TINYFILEDIALOGS_H
#define TINYFILEDIALOGS_H

#ifdef __cplusplus
extern "C" {
#endif

extern char tinyfd_version[8];

#ifdef _WIN32
extern int tinyfd_winUtf8;
#endif

extern int tinyfd_forceConsole;
extern char tinyfd_response[1024];

int tinyfd_messageBox(
    char const * const aTitle,
    char const * const aMessage,
    char const * const aDialogType,
    char const * const aIconType,
    int const aDefaultButton);

char const * tinyfd_inputBox(
    char const * const aTitle,
    char const * const aMessage,
    char const * const aDefaultInput);

char const * tinyfd_saveFileDialog(
    char const * const aTitle,
    char const * const aDefaultPathAndFile,
    int const aNumOfFilterPatterns,
    char const * const * const aFilterPatterns,
    char const * const aSingleFilterDescription);

char const * tinyfd_openFileDialog(
    char const * const aTitle,
    char const * const aDefaultPathAndFile,
    int const aNumOfFilterPatterns,
    char const * const * const aFilterPatterns,
    char const * const aSingleFilterDescription,
    int const aAllowMultipleSelects);

char const * tinyfd_selectFolderDialog(
    char const * const aTitle,
    char const * const aDefaultPath);

char const * tinyfd_colorChooser(
    char const * const aTitle,
    char const * const aDefaultHexRGB,
    unsigned char const aDefaultRGB[3],
    unsigned char aoResultRGB[3]);

#ifdef _WIN32
#ifndef TINYFD_NOLIB
int tinyfd_messageBoxW(
    wchar_t const * const aTitle,
    wchar_t const * const aMessage,
    wchar_t const * const aDialogType,
    wchar_t const * const aIconType,
    int const aDefaultButton);

wchar_t const * tinyfd_saveFileDialogW(
    wchar_t const * const aTitle,
    wchar_t const * const aDefaultPathAndFile,
    int const aNumOfFilterPatterns,
    wchar_t const * const * const aFilterPatterns,
    wchar_t const * const aSingleFilterDescription);

wchar_t const * tinyfd_openFileDialogW(
    wchar_t const * const aTitle,
    wchar_t const * const aDefaultPathAndFile,
    int const aNumOfFilterPatterns,
    wchar_t const * const * const aFilterPatterns,
    wchar_t const * const aSingleFilterDescription,
    int const aAllowMultipleSelects);

wchar_t const * tinyfd_selectFolderDialogW(
    wchar_t const * const aTitle,
    wchar_t const * const aDefaultPath);

wchar_t const * tinyfd_colorChooserW(
    wchar_t const * const aTitle,
    wchar_t const * const aDefaultHexRGB,
    unsigned char const aDefaultRGB[3],
    unsigned char aoResultRGB[3]);
#endif /* TINYFD_NOLIB */
#else
char const * tinyfd_arrayDialog(
    char const * const aTitle,
    int const aNumOfColumns,
    char const * const * const aColumns,
    int const aNumOfRows,
    char const * const * const aCells);
#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* TINYFILEDIALOGS_H */
