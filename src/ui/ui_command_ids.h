// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Single stable UI ID registry; never declare command/control IDs elsewhere.

#pragma once

#include <array>

// Stable Win32 command and control identifiers.
//
// Keep every UI ID in this file so command routing, control creation, menus,
// tests, and documentation share one authoritative registry. Values are part
// of the persisted/native UI contract and should not be renumbered casually.
namespace quicksift::ui::command_id {

inline constexpr int ID_PICK_FOLDER = 1001;
inline constexpr int ID_MODE_THUMBS = 1002;
inline constexpr int ID_MODE_SINGLE = 1003;
inline constexpr int ID_MODE_COMPARE = 1004;
inline constexpr int ID_COPY = 1005;
inline constexpr int ID_MOVE = 1006;
inline constexpr int ID_DELETE = 1007;
inline constexpr int ID_FORMATS = 1008;
inline constexpr int ID_SORT = 1009;
inline constexpr int ID_THUMB_SIZE = 1010;
inline constexpr int ID_PREVIOUS = 1011;
inline constexpr int ID_NEXT = 1012;
inline constexpr int ID_DATE_FILTER = 1013;
inline constexpr int ID_RATING_MENU = 1014;
inline constexpr int ID_RATE_1 = 1021;
inline constexpr int ID_RATE_2 = 1022;
inline constexpr int ID_RATE_3 = 1023;
inline constexpr int ID_RATE_4 = 1024;
inline constexpr int ID_RATE_5 = 1025;
inline constexpr int ID_RATE_CLEAR = 1026;
inline constexpr int ID_PICK = 1027;
inline constexpr int ID_REJECT = 1028;
inline constexpr int ID_CLEAR_CULL = 1029;
inline constexpr int ID_COLOR_LABEL = 1030;
inline constexpr int ID_ZOOM_MODE = 1032;
inline constexpr int ID_ROTATE_LEFT = 1033;
inline constexpr int ID_ROTATE_RIGHT = 1034;
inline constexpr int ID_SYNC_VIEW = 1035;
inline constexpr int ID_FACE_LOCK = 1036;
inline constexpr int ID_SETTINGS = 1037;
inline constexpr int ID_UNDO = 1038;
inline constexpr int ID_REDO = 1039;
inline constexpr int ID_STAR_FILTER = 1040;
inline constexpr int ID_PICK_FILTER = 1041;
inline constexpr int ID_FILTER_LABEL = 1042;
inline constexpr int ID_THEME_TOGGLE = 1043;
inline constexpr int ID_HELP = 1044;
inline constexpr int ID_LABEL_FILTER = 1045;
inline constexpr int ID_FULLSCREEN = 1046;
inline constexpr int ID_FILMSTRIP = 1047;
inline constexpr int ID_PICK_STATE_MENU = 1048;
inline constexpr int ID_PICK_STATE_PICK = 1049;
inline constexpr int ID_PICK_STATE_REJECT = 1050;
inline constexpr int ID_PICK_STATE_UNMARK = 1051;
inline constexpr int ID_FMT_JPEG = 2001;
inline constexpr int ID_FMT_PNG = 2002;
inline constexpr int ID_FMT_HEIF = 2003;
inline constexpr int ID_FMT_TIFF = 2004;
inline constexpr int ID_FMT_BASIC = 2005;
inline constexpr int ID_FMT_WEBP = 2006;
inline constexpr int ID_FMT_RAW = 2007;
inline constexpr int ID_FMT_ALL = 2008;
inline constexpr int ID_SORT_NAME_ASC = 2101;
inline constexpr int ID_SORT_NAME_DESC = 2102;
inline constexpr int ID_SORT_NEWEST = 2103;
inline constexpr int ID_SORT_OLDEST = 2104;
inline constexpr int ID_SORT_RATING = 2105;
inline constexpr int ID_SORT_EXTENSION = 2106;
inline constexpr int ID_SIZE_TINY = 2201;
inline constexpr int ID_SIZE_SMALL = 2202;
inline constexpr int ID_SIZE_MEDIUM = 2203;
inline constexpr int ID_SIZE_LARGE = 2204;
inline constexpr int ID_SIZE_HUGE = 2205;
inline constexpr int ID_DATE_ANY = 2301;
inline constexpr int ID_DATE_24H = 2302;
inline constexpr int ID_DATE_7D = 2303;
inline constexpr int ID_DATE_30D = 2304;
inline constexpr int ID_DATE_1Y = 2305;
inline constexpr int ID_LABEL_NONE = 2400;
inline constexpr int ID_LABEL_RED = 2401;
inline constexpr int ID_LABEL_YELLOW = 2402;
inline constexpr int ID_LABEL_GREEN = 2403;
inline constexpr int ID_LABEL_BLUE = 2404;
inline constexpr int ID_LABEL_PURPLE = 2405;
inline constexpr int ID_RATING_FILTER_ALL = 2500;
inline constexpr int ID_RATING_FILTER_NONE = 2501;
inline constexpr int ID_RATING_FILTER_1 = 2502;
inline constexpr int ID_RATING_FILTER_2 = 2503;
inline constexpr int ID_RATING_FILTER_3 = 2504;
inline constexpr int ID_RATING_FILTER_4 = 2505;
inline constexpr int ID_RATING_FILTER_5 = 2506;
inline constexpr int ID_RATING_FILTER_4PLUS = 2507;
inline constexpr int ID_RATING_FILTER_5PLUS = 2508;
inline constexpr int ID_PICK_FILTER_ALL = 2520;
inline constexpr int ID_PICK_FILTER_PICKS = 2521;
inline constexpr int ID_PICK_FILTER_REJECTS = 2522;
inline constexpr int ID_PICK_FILTER_UNMARKED = 2523;
inline constexpr int ID_LABEL_FILTER_ALL = 2540;
inline constexpr int ID_LABEL_FILTER_NONE = 2541;
inline constexpr int ID_LABEL_FILTER_RED = 2542;
inline constexpr int ID_LABEL_FILTER_YELLOW = 2543;
inline constexpr int ID_LABEL_FILTER_GREEN = 2544;
inline constexpr int ID_LABEL_FILTER_BLUE = 2545;
inline constexpr int ID_LABEL_FILTER_PURPLE = 2546;
inline constexpr int ID_ZOOM_FIT = 2600;
inline constexpr int ID_ZOOM_FIT_WIDTH = 2601;
inline constexpr int ID_ZOOM_FIT_HEIGHT = 2602;
inline constexpr int ID_ZOOM_100 = 2603;
inline constexpr int ID_ZOOM_FILL = 2604;
inline constexpr int ID_SETTING_BADGE_STARS = 2701;
inline constexpr int ID_SETTING_BADGE_COLOR = 2702;
inline constexpr int ID_SETTING_BADGE_PICK = 2703;
inline constexpr int ID_SETTING_BADGE_PAIR = 2704;
inline constexpr int ID_SETTING_THEME = 2706;
inline constexpr int ID_SETTING_DIRECT_JPEG = 2707;
inline constexpr int ID_SETTING_DIRECT_PNG = 2708;
inline constexpr int ID_SETTING_DIRECT_TIFF = 2709;
inline constexpr int ID_SETTING_DIRECT_RAW = 2710;
inline constexpr int ID_SETTING_SAFE_JPEG = 2711;
inline constexpr int ID_SETTING_LANGUAGE_ENGLISH = 2712;
inline constexpr int ID_SETTING_LANGUAGE_VIETNAMESE = 2713;
inline constexpr int ID_SETTING_PERFORMANCE_HUD = 2714;
inline constexpr int ID_SETTING_CLEAR_MEMORY_CACHE = 2721;
inline constexpr int ID_SETTING_CLEAR_DISK_CACHE = 2715;
inline constexpr int ID_SETTING_ABOUT = 2716;
inline constexpr int ID_SETTING_HELP = 2717;
inline constexpr int ID_SETTING_DIAGNOSTIC_LOG = 2719;
inline constexpr int ID_SETTING_COPY_MOVE_RAW_WITH_JPG = 2718;
inline constexpr int ID_SETTING_RAW_JPEG_PREVIEWS_ONLY = 2720;
inline constexpr int ID_SETTING_METADATA_RAW = 2722;
inline constexpr int ID_SETTING_METADATA_LOSSY = 2723;
inline constexpr int ID_SETTING_METADATA_TIFF = 2724;
inline constexpr int ID_FOLDER_TREE = 3001;
inline constexpr int ID_CANVAS = 3002;
inline constexpr int ID_STATUS_TEXT = 3003;
inline constexpr int ID_EXIF_PANEL = 3004;
inline constexpr int ID_FLYOUT_BACKDROP = 3005;
inline constexpr int ID_FULLSCREEN_TITLE_OVERLAY = 3006;
inline constexpr int ID_FLYOUT_TAB_CULL = 3010;
inline constexpr int ID_FLYOUT_TAB_FILTER = 3011;
inline constexpr int ID_FLYOUT_TAB_FOLDERS = 3012;
inline constexpr int ID_FLYOUT_TAB_INFO = 3013;
inline constexpr int ID_FLYOUT_TAB_FILE = 3014;
inline constexpr int ID_FLYOUT_TAB_SETTINGS = 3015;
inline constexpr int ID_LIBRARY_LIST = 3016;
inline constexpr int ID_REMOVE_LIBRARY = 3017;
inline constexpr int ID_USER_COMMENT_EDIT = 3018;
inline constexpr int ID_SAVE_USER_COMMENT = 3019;
inline constexpr int ID_SETTING_LANGUAGE_SELECTOR = 3020;
inline constexpr int ID_SETTING_SECTION_THUMBNAILS = 3021;
inline constexpr int ID_SETTING_SECTION_RAW = 3022;
inline constexpr int ID_SETTING_SECTION_METADATA = 3023;
inline constexpr int ID_SETTING_SECTION_DIAGNOSTICS = 3024;
inline constexpr int ID_SETTING_SECTION_LANGUAGE = 3025;

inline constexpr int ID_DOCUMENT_TEXT = 5001;
inline constexpr int ID_DOCUMENT_EXPORT = 5002;
inline constexpr int ID_DOCUMENT_REFRESH = 5003;
inline constexpr int ID_DOCUMENT_VERBOSE = 5004;

inline constexpr std::array kAllIds{
    ID_PICK_FOLDER,
    ID_MODE_THUMBS,
    ID_MODE_SINGLE,
    ID_MODE_COMPARE,
    ID_COPY,
    ID_MOVE,
    ID_DELETE,
    ID_FORMATS,
    ID_SORT,
    ID_THUMB_SIZE,
    ID_PREVIOUS,
    ID_NEXT,
    ID_DATE_FILTER,
    ID_RATE_1,
    ID_RATE_2,
    ID_RATE_3,
    ID_RATE_4,
    ID_RATE_5,
    ID_RATE_CLEAR,
    ID_PICK,
    ID_REJECT,
    ID_CLEAR_CULL,
    ID_COLOR_LABEL,
    ID_ZOOM_MODE,
    ID_ROTATE_LEFT,
    ID_ROTATE_RIGHT,
    ID_SYNC_VIEW,
    ID_FACE_LOCK,
    ID_SETTINGS,
    ID_UNDO,
    ID_REDO,
    ID_STAR_FILTER,
    ID_PICK_FILTER,
    ID_FILTER_LABEL,
    ID_THEME_TOGGLE,
    ID_HELP,
    ID_LABEL_FILTER,
    ID_FULLSCREEN,
    ID_FILMSTRIP,
    ID_PICK_STATE_MENU, ID_PICK_STATE_PICK, ID_PICK_STATE_REJECT, ID_PICK_STATE_UNMARK,
    ID_FMT_JPEG,
    ID_FMT_PNG,
    ID_FMT_HEIF,
    ID_FMT_TIFF,
    ID_FMT_BASIC,
    ID_FMT_WEBP,
    ID_FMT_RAW,
    ID_FMT_ALL,
    ID_SORT_NAME_ASC,
    ID_SORT_NAME_DESC,
    ID_SORT_NEWEST,
    ID_SORT_OLDEST,
    ID_SORT_RATING,
    ID_SORT_EXTENSION,
    ID_SIZE_TINY,
    ID_SIZE_SMALL,
    ID_SIZE_MEDIUM,
    ID_SIZE_LARGE,
    ID_SIZE_HUGE,
    ID_DATE_ANY,
    ID_DATE_24H,
    ID_DATE_7D,
    ID_DATE_30D,
    ID_DATE_1Y,
    ID_LABEL_NONE,
    ID_LABEL_RED,
    ID_LABEL_YELLOW,
    ID_LABEL_GREEN,
    ID_LABEL_BLUE,
    ID_LABEL_PURPLE,
    ID_RATING_FILTER_ALL,
    ID_RATING_FILTER_NONE,
    ID_RATING_FILTER_1,
    ID_RATING_FILTER_2,
    ID_RATING_FILTER_3,
    ID_RATING_FILTER_4,
    ID_RATING_FILTER_5,
    ID_RATING_FILTER_4PLUS,
    ID_RATING_FILTER_5PLUS,
    ID_PICK_FILTER_ALL,
    ID_PICK_FILTER_PICKS,
    ID_PICK_FILTER_REJECTS,
    ID_PICK_FILTER_UNMARKED,
    ID_LABEL_FILTER_ALL,
    ID_LABEL_FILTER_NONE,
    ID_LABEL_FILTER_RED,
    ID_LABEL_FILTER_YELLOW,
    ID_LABEL_FILTER_GREEN,
    ID_LABEL_FILTER_BLUE,
    ID_LABEL_FILTER_PURPLE,
    ID_ZOOM_FIT,
    ID_ZOOM_FIT_WIDTH,
    ID_ZOOM_FIT_HEIGHT,
    ID_ZOOM_100,
    ID_ZOOM_FILL,
    ID_SETTING_BADGE_STARS,
    ID_SETTING_BADGE_COLOR,
    ID_SETTING_BADGE_PICK,
    ID_SETTING_BADGE_PAIR,
    ID_SETTING_THEME,
    ID_SETTING_DIRECT_JPEG,
    ID_SETTING_DIRECT_PNG,
    ID_SETTING_DIRECT_TIFF,
    ID_SETTING_DIRECT_RAW,
    ID_SETTING_SAFE_JPEG,
    ID_SETTING_LANGUAGE_ENGLISH,
    ID_SETTING_LANGUAGE_VIETNAMESE,
    ID_SETTING_CLEAR_MEMORY_CACHE,
    ID_SETTING_CLEAR_DISK_CACHE,
    ID_SETTING_ABOUT,
    ID_SETTING_HELP,
    ID_SETTING_DIAGNOSTIC_LOG,
    ID_SETTING_COPY_MOVE_RAW_WITH_JPG,
    ID_SETTING_RAW_JPEG_PREVIEWS_ONLY,
    ID_FOLDER_TREE,
    ID_CANVAS,
    ID_STATUS_TEXT,
    ID_EXIF_PANEL,
    ID_USER_COMMENT_EDIT,
    ID_SAVE_USER_COMMENT,
    ID_SETTING_LANGUAGE_SELECTOR,
    ID_FLYOUT_BACKDROP,
    ID_FULLSCREEN_TITLE_OVERLAY,
    ID_FLYOUT_TAB_FILE,
    ID_FLYOUT_TAB_CULL,
    ID_FLYOUT_TAB_FILTER,
    ID_FLYOUT_TAB_FOLDERS,
    ID_FLYOUT_TAB_INFO,
    ID_FLYOUT_TAB_SETTINGS,
    ID_DOCUMENT_TEXT,
    ID_DOCUMENT_EXPORT,
    ID_DOCUMENT_REFRESH,
    ID_DOCUMENT_VERBOSE,
};

} // namespace quicksift::ui::command_id
