// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Authoritative localized string tables and lookup behavior.

#include "localization.h"
#include "version.h"

#include <array>
#include <initializer_list>
#include <algorithm>
#include <utility>

namespace quicksift {
namespace {

struct TranslationPair {
    std::wstring_view english;
    std::wstring_view vietnamese;
};

// Literal source text is deliberately the key. It lets old and newly added UI
// paths opt into localization without introducing numeric resource IDs or a
// second copy of every menu layout.
const std::initializer_list<TranslationPair> kTranslations{
    TranslationPair{L"QuickSift", L"QuickSift"},
    TranslationPair{L"Selection is intentionally limited to visible results; hidden photos were removed from the selection.",
        L"Vùng chọn được giới hạn có chủ ý trong các kết quả đang hiển thị; ảnh bị ẩn đã được bỏ khỏi vùng chọn."},
    TranslationPair{L"selection limited to visible results",
        L"vùng chọn giới hạn trong kết quả đang hiển thị"},
    TranslationPair{L"Mixed", L"Hỗn hợp"},
    TranslationPair{L"Reading…", L"Đang đọc…"},
    TranslationPair{L"Custom", L"Tùy chỉnh"},
    TranslationPair{L"Unreadable", L"Không đọc được"},
    TranslationPair{L"Rating", L"Xếp hạng"},
    TranslationPair{L"Rating pending", L"Đang đọc xếp hạng"},
    TranslationPair{L"Rating unreadable", L"Không đọc được xếp hạng"},
    TranslationPair{L"Custom rating", L"Xếp hạng tùy chỉnh"},
    TranslationPair{L"Pick state pending", L"Đang đọc trạng thái chọn"},
    TranslationPair{L"Pick state unreadable", L"Không đọc được trạng thái chọn"},
    TranslationPair{L"Custom pick state", L"Trạng thái chọn tùy chỉnh"},
    TranslationPair{L"Reading metadata for the current filters",
        L"Đang đọc metadata cho các bộ lọc hiện tại"},
    TranslationPair{L"No photos match the current filters",
        L"Không có ảnh nào khớp với các bộ lọc hiện tại"},
    TranslationPair{L"QuickSift — Fast Photo Shortlisting", L"QuickSift — Chọn lọc ảnh nhanh"},
    TranslationPair{L"Open", L"Mở"},
    TranslationPair{L"Thumbnails", L"Ảnh thu nhỏ"},
    TranslationPair{L"Single", L"Một ảnh"},
    TranslationPair{L"Compare", L"So sánh"},
    TranslationPair{L"← Previous", L"← Trước"},
    TranslationPair{L"Next →", L"Sau →"},
    TranslationPair{L"Copy", L"Sao chép"},
    TranslationPair{L"Move", L"Di chuyển"},
    TranslationPair{L"Delete", L"Xóa"},
    TranslationPair{L"Undo", L"Hoàn tác"},
    TranslationPair{L"Redo", L"Làm lại"},
    TranslationPair{L"Format: All", L"Định dạng: Tất cả"},
    TranslationPair{L"Format: None", L"Định dạng: Không có"},
    TranslationPair{L"Date: Any", L"Ngày: Bất kỳ"},
    TranslationPair{L"Date: 24h", L"Ngày: 24 giờ"},
    TranslationPair{L"Date: 7d", L"Ngày: 7 ngày"},
    TranslationPair{L"Date: 30d", L"Ngày: 30 ngày"},
    TranslationPair{L"Date: 1y", L"Ngày: 1 năm"},
    TranslationPair{L"Sort: A–Z", L"Sắp xếp: A–Z"},
    TranslationPair{L"Sort: Z–A", L"Sắp xếp: Z–A"},
    TranslationPair{L"Sort: Newest", L"Sắp xếp: Mới nhất"},
    TranslationPair{L"Sort: Oldest", L"Sắp xếp: Cũ nhất"},
    TranslationPair{L"Sort: Rating", L"Sắp xếp: Xếp hạng"},
    TranslationPair{L"Sort: Format", L"Sắp xếp: Định dạng"},
    TranslationPair{L"Thumbnail size: XS", L"Cỡ ảnh thu nhỏ: XS"},
    TranslationPair{L"Thumbnail size: S", L"Cỡ ảnh thu nhỏ: S"},
    TranslationPair{L"Thumbnail size: M", L"Cỡ ảnh thu nhỏ: M"},
    TranslationPair{L"Thumbnail size: L", L"Cỡ ảnh thu nhỏ: L"},
    TranslationPair{L"Thumbnail size: XL", L"Cỡ ảnh thu nhỏ: XL"},
    TranslationPair{L"Stars: All", L"Sao: Tất cả"},
    TranslationPair{L"Stars: 1", L"Sao: 1"},
    TranslationPair{L"Stars: 2", L"Sao: 2"},
    TranslationPair{L"Stars: 3", L"Sao: 3"},
    TranslationPair{L"Stars: 4", L"Sao: 4"},
    TranslationPair{L"Stars: 4+", L"Sao: 4+"},
    TranslationPair{L"Stars: 5", L"Sao: 5"},
    TranslationPair{L"Pick state: All", L"Trạng thái chọn: Tất cả"},
    TranslationPair{L"Pick", L"Chọn"},
    TranslationPair{L"Reject", L"Loại"},
    TranslationPair{L"Unmark", L"Bỏ đánh dấu"},
    TranslationPair{L"Label: None", L"Nhãn: Không"},
    TranslationPair{L"Labels: All", L"Nhãn: Tất cả"},
    TranslationPair{L"Labels: None", L"Nhãn: Không"},
    TranslationPair{L"Labels: Red", L"Nhãn: Đỏ"},
    TranslationPair{L"Labels: Yellow", L"Nhãn: Vàng"},
    TranslationPair{L"Labels: Green", L"Nhãn: Xanh lá"},
    TranslationPair{L"Labels: Blue", L"Nhãn: Xanh dương"},
    TranslationPair{L"Labels: Purple", L"Nhãn: Tím"},
    TranslationPair{L"Zoom: Fit", L"Thu phóng: Vừa khung"},
    TranslationPair{L"Rotate left", L"Xoay trái"},
    TranslationPair{L"Rotate right", L"Xoay phải"},
    TranslationPair{L"Sync: On", L"Đồng bộ: Bật"},
    TranslationPair{L"Sync: Off", L"Đồng bộ: Tắt"},
    TranslationPair{L"Face Lock: On", L"Khóa khuôn mặt: Bật"},
    TranslationPair{L"Face Lock: Off", L"Khóa khuôn mặt: Tắt"},
    TranslationPair{L"Face Lock: Unavailable", L"Khóa khuôn mặt: Không khả dụng"},
    TranslationPair{L"Face Lock: Temporarily unavailable", L"Khóa khuôn mặt: Tạm thời không khả dụng"},
    TranslationPair{L"Fullscreen", L"Toàn màn hình"},
    TranslationPair{L"Exit fullscreen", L"Thoát toàn màn hình"},
    TranslationPair{L"Settings", L"Cài đặt"},
    TranslationPair{L"File", L"Tệp"},
    TranslationPair{L"Pick state:", L"Trạng thái chọn:"},
    TranslationPair{L"Help", L"Trợ giúp"},
    TranslationPair{L"About", L"Giới thiệu"},
    TranslationPair{L"English", L"Tiếng Anh"},
    TranslationPair{L"Vietnamese", L"Tiếng Việt"},
    TranslationPair{L"Language: English", L"Ngôn ngữ: Tiếng Anh"},
    TranslationPair{L"Language: Vietnamese", L"Ngôn ngữ: Tiếng Việt"},
    TranslationPair{L"Dark", L"Tối"},
    TranslationPair{L"Light", L"Sáng"},
    TranslationPair{L"Filter by:", L"Lọc theo:"},
    TranslationPair{L"Filter by label", L"Lọc theo nhãn"},
    TranslationPair{L"FOLDERS", L"THƯ MỤC"},
        {L"LIBRARY", L"THƯ VIỆN"},
    TranslationPair{L"INFO", L"THÔNG TIN"},
    TranslationPair{L"CULL & RATE", L"CHỌN & XẾP HẠNG"},
    TranslationPair{L"FILTER", L"BỘ LỌC"},
    TranslationPair{L"Choose a folder to begin.", L"Chọn một thư mục để bắt đầu."},
    TranslationPair{L"Open a folder to begin", L"Mở một thư mục để bắt đầu"},
    TranslationPair{L"No included photos in this folder", L"Không có ảnh phù hợp trong thư mục này"},
    TranslationPair{L"Select a photo to inspect its EXIF information.", L"Chọn một ảnh để xem thông tin EXIF."},
    TranslationPair{L"Loading image…", L"Đang tải ảnh…"},
    TranslationPair{L"Loading…", L"Đang tải…"},
    TranslationPair{L"loading…", L"đang tải…"},
    TranslationPair{L"Red border: the largest detected face appears quite blurry or out of focus. Medium sensitivity flags only clear focus misses; treat this as a warning, not a definitive focus test.",
        L"Viền đỏ: khuôn mặt lớn nhất được phát hiện có vẻ khá nhòe hoặc lệch nét. Độ nhạy trung bình chỉ đánh dấu các trường hợp hụt nét rõ ràng; đây là cảnh báo, không phải phép kiểm tra nét tuyệt đối."},
    TranslationPair{L"Face Lock centers the view on the largest detected face. Detection runs locally through Windows Media FaceAnalysis.",
        L"Khóa khuôn mặt căn vùng nhìn vào khuôn mặt lớn nhất được phát hiện. Việc phát hiện chạy cục bộ qua Windows Media FaceAnalysis."},
    TranslationPair{L"Face Lock is unavailable because Windows reports that face detection is not supported on this device.",
        L"Khóa khuôn mặt không khả dụng vì Windows báo rằng thiết bị này không hỗ trợ phát hiện khuôn mặt."},
    TranslationPair{L"Windows face detection could not be initialized. QuickSift will retry automatically.",
        L"Không thể khởi tạo phát hiện khuôn mặt của Windows. QuickSift sẽ tự động thử lại."},
    TranslationPair{L"Checking Windows face-detection support…", L"Đang kiểm tra hỗ trợ phát hiện khuôn mặt của Windows…"},
    TranslationPair{L"Face Lock unavailable: Windows does not support face detection on this device.",
        L"Khóa khuôn mặt không khả dụng: Windows không hỗ trợ phát hiện khuôn mặt trên thiết bị này."},
    TranslationPair{L"Face Lock temporarily unavailable: QuickSift will retry Windows face detection.",
        L"Khóa khuôn mặt tạm thời không khả dụng: QuickSift sẽ thử lại tính năng phát hiện khuôn mặt của Windows."},
    TranslationPair{L"Face analysis paused for this photo after repeated temporary failures. Toggle Face Lock to retry now.",
        L"Phân tích khuôn mặt đã tạm dừng cho ảnh này sau nhiều lỗi tạm thời. Chuyển trạng thái Khóa khuôn mặt để thử lại ngay."},
    TranslationPair{L"Show star badges", L"Hiện huy hiệu số sao"},
    TranslationPair{L"Show color-label badges", L"Hiện huy hiệu nhãn màu"},
    TranslationPair{L"Show pick/reject badges", L"Hiện huy hiệu chọn/loại"},
    TranslationPair{L"Show RAW/JPG badges", L"Hiện huy hiệu RAW/JPG"},
    TranslationPair{L"Copy/Move RAW with JPG", L"Sao chép/Di chuyển RAW cùng JPG"},
    TranslationPair{L"Load only JPG previews for RAW", L"Chỉ tải ảnh xem trước JPG cho RAW"},
    TranslationPair{L"RAW decoding disabled; using embedded JPG previews only.",
        L"Đã tắt giải mã RAW; chỉ sử dụng ảnh xem trước JPG nhúng."},
    TranslationPair{L"Full RAW decoding enabled.", L"Đã bật giải mã RAW đầy đủ."},
    TranslationPair{L"Copying", L"Đang sao chép"},
    TranslationPair{L"Moving", L"Đang di chuyển"},
    TranslationPair{L"Deleting", L"Đang xóa"},
    TranslationPair{L"complete file", L"tệp hoàn tất"},
    TranslationPair{L"complete files", L"tệp hoàn tất"},
    TranslationPair{L"total file", L"tổng số tệp"},
    TranslationPair{L"total files", L"tổng số tệp"},
    TranslationPair{L"calculating time remaining", L"đang tính thời gian còn lại"},
    TranslationPair{L"minute remaining", L"phút còn lại"},
    TranslationPair{L"minutes remaining", L"phút còn lại"},
    TranslationPair{L"File operations are still running", L"Các thao tác tệp vẫn đang chạy"},
    TranslationPair{L"QuickSift is still changing files. Closing anyway will cancel pending operations at the next safe file boundary. Already completed and verified files will remain complete; QuickSift will not stop in the middle of an atomic media group.",
        L"QuickSift vẫn đang thay đổi tệp. Đóng ứng dụng sẽ hủy các thao tác đang chờ tại ranh giới tệp an toàn tiếp theo. Các tệp đã hoàn tất và xác minh sẽ được giữ nguyên; QuickSift sẽ không dừng giữa một nhóm phương tiện nguyên tử."},
    TranslationPair{L"Close anyway", L"Vẫn đóng"},
    TranslationPair{L"Wait", L"Chờ"},
    TranslationPair{L"Cancelling file operations at a safe boundary before closing…",
        L"Đang hủy thao tác tệp tại ranh giới an toàn trước khi đóng…"},
    TranslationPair{L"Dark appearance", L"Giao diện tối"},
    TranslationPair{L"Enable all", L"Bật tất cả"},
    TranslationPair{L"Camera RAW", L"RAW máy ảnh"},
    TranslationPair{L"Any modified date", L"Mọi ngày chỉnh sửa"},
    TranslationPair{L"Modified in last 24 hours", L"Chỉnh sửa trong 24 giờ qua"},
    TranslationPair{L"Modified in last 7 days", L"Chỉnh sửa trong 7 ngày qua"},
    TranslationPair{L"Modified in last 30 days", L"Chỉnh sửa trong 30 ngày qua"},
    TranslationPair{L"Modified in last year", L"Chỉnh sửa trong năm qua"},
    TranslationPair{L"Name — A to Z", L"Tên — A đến Z"},
    TranslationPair{L"Name — Z to A", L"Tên — Z đến A"},
    TranslationPair{L"Newest first", L"Mới nhất trước"},
    TranslationPair{L"Oldest first", L"Cũ nhất trước"},
    TranslationPair{L"Highest rating first", L"Xếp hạng cao trước"},
    TranslationPair{L"File format", L"Định dạng tệp"},
    TranslationPair{L"Fit", L"Vừa khung"},
    TranslationPair{L"Fit width", L"Vừa chiều rộng"},
    TranslationPair{L"Fit height", L"Vừa chiều cao"},
    TranslationPair{L"100%", L"100%"},
    TranslationPair{L"Fill", L"Lấp đầy"},
    TranslationPair{L"Choose a photo folder", L"Chọn thư mục ảnh"},
    TranslationPair{L"Information", L"Thông tin"},
    TranslationPair{L"Warning", L"Cảnh báo"},
    TranslationPair{L"Error", L"Lỗi"},
    TranslationPair{L"OK", L"Đồng ý"},
    TranslationPair{L"Cancel", L"Hủy"},
    TranslationPair{L"Move to Deleted", L"Chuyển vào Deleted"},
    TranslationPair{L"Move selected photos to Deleted?", L"Chuyển ảnh đã chọn vào Deleted?"},
    TranslationPair{L"Close", L"Đóng"},
    TranslationPair{L"QuickSift Help", L"Trợ giúp QuickSift"},
    TranslationPair{L"About QuickSift", L"Giới thiệu QuickSift"},
    TranslationPair{L"Automatic (recommended)", L"Tự động (khuyến nghị)"},
    TranslationPair{L"Performance profile", L"Cấu hình hiệu năng"},
    TranslationPair{L"Clear memory cache", L"Xóa bộ nhớ đệm RAM"},
    TranslationPair{L"Clear disk cache", L"Xóa bộ nhớ đệm ổ đĩa"},
    TranslationPair{L"Cache cleared.", L"Đã xóa bộ nhớ đệm."},
    TranslationPair{L"Language changed.", L"Đã đổi ngôn ngữ."},
    TranslationPair{L"Done copying", L"Đã sao chép xong"},
    TranslationPair{L"Done moving", L"Đã di chuyển xong"},
    TranslationPair{L"At least one corresponding RAW(s) is missing, please check.",
        L"Thiếu ít nhất một tệp RAW tương ứng, vui lòng kiểm tra."},
    TranslationPair{L"RAW pairing check failed — nothing changed", L"Không thể kiểm tra cặp RAW — chưa thay đổi tệp nào"},
    TranslationPair{L"QuickSift could not safely inspect the source folder for corresponding RAW files. It stopped before copying or moving anything.", L"QuickSift không thể kiểm tra an toàn thư mục nguồn để tìm tệp RAW tương ứng. Ứng dụng đã dừng trước khi sao chép hoặc di chuyển bất kỳ tệp nào."},
    TranslationPair{L"Move selected photos to", L"Di chuyển ảnh đã chọn đến"},
    TranslationPair{L"Copy selected photos to", L"Sao chép ảnh đã chọn đến"},
    TranslationPair{L"Help and documentation", L"Trợ giúp và tài liệu"},
    TranslationPair{L"About this app", L"Giới thiệu ứng dụng"},
    TranslationPair{L"All ratings", L"Tất cả xếp hạng"},
    TranslationPair{L"No stars", L"Không có sao"},
    TranslationPair{L"Exactly 1 star", L"Đúng 1 sao"},
    TranslationPair{L"Exactly 2 stars", L"Đúng 2 sao"},
    TranslationPair{L"Exactly 3 stars", L"Đúng 3 sao"},
    TranslationPair{L"Exactly 4 stars", L"Đúng 4 sao"},
    TranslationPair{L"Exactly 5 stars", L"Đúng 5 sao"},
    TranslationPair{L"4 stars and up", L"Từ 4 sao trở lên"},
    TranslationPair{L"All pick states", L"Tất cả trạng thái chọn"},
    TranslationPair{L"Picks only", L"Chỉ ảnh đã chọn"},
    TranslationPair{L"Rejected only", L"Chỉ ảnh đã loại"},
    TranslationPair{L"Unmarked only", L"Chỉ ảnh chưa đánh dấu"},
    TranslationPair{L"All labels", L"Tất cả nhãn"},
    TranslationPair{L"Unlabeled only", L"Chỉ ảnh chưa gắn nhãn"},
    TranslationPair{L"Red only", L"Chỉ nhãn đỏ"},
    TranslationPair{L"Yellow only", L"Chỉ nhãn vàng"},
    TranslationPair{L"Green only", L"Chỉ nhãn xanh lá"},
    TranslationPair{L"Blue only", L"Chỉ nhãn xanh dương"},
    TranslationPair{L"Purple only", L"Chỉ nhãn tím"},
    TranslationPair{L"Fit image", L"Vừa toàn bộ ảnh"},
    TranslationPair{L"100% pixels", L"Điểm ảnh 100%"},
    TranslationPair{L"Fill pane", L"Lấp đầy khung"},
    TranslationPair{L"JPEG", L"JPEG"},
    TranslationPair{L"PNG", L"PNG"},
    TranslationPair{L"AVIF / HEIF", L"AVIF / HEIF"},
    TranslationPair{L"TIFF", L"TIFF"},
    TranslationPair{L"BMP / GIF", L"BMP / GIF"},
    TranslationPair{L"WebP", L"WebP"},
    TranslationPair{L"Name A–Z", L"Tên A–Z"},
    TranslationPair{L"Name Z–A", L"Tên Z–A"},
    TranslationPair{L"Rating: high first", L"Xếp hạng: cao trước"},
    TranslationPair{L"Tiny", L"Rất nhỏ"},
    TranslationPair{L"Small", L"Nhỏ"},
    TranslationPair{L"Medium", L"Vừa"},
    TranslationPair{L"Large", L"Lớn"},
    TranslationPair{L"Huge", L"Rất lớn"},
    TranslationPair{L"JPEG metadata: Inside file", L"Metadata JPEG: Trong tệp"},
    TranslationPair{L"JPEG metadata: XMP sidecar", L"Metadata JPEG: Tệp XMP sidecar"},
    TranslationPair{L"PNG metadata: Inside file", L"Metadata PNG: Trong tệp"},
    TranslationPair{L"PNG metadata: XMP sidecar", L"Metadata PNG: Tệp XMP sidecar"},
    TranslationPair{L"TIFF metadata: Inside file", L"Metadata TIFF: Trong tệp"},
    TranslationPair{L"TIFF metadata: XMP sidecar", L"Metadata TIFF: Tệp XMP sidecar"},
    TranslationPair{L"RAW metadata: Inside file (advanced)", L"Metadata RAW: Trong tệp (nâng cao)"},
    TranslationPair{L"RAW metadata: XMP sidecar", L"Metadata RAW: Tệp XMP sidecar"},
    TranslationPair{L"Safe JPEG writes: On (verified copy)", L"Ghi JPEG an toàn: Bật (bản sao đã xác minh)"},
    TranslationPair{L"Safe JPEG writes: Off (faster)", L"Ghi JPEG an toàn: Tắt (nhanh hơn)"},
    TranslationPair{L"Safe JPEG writes enabled", L"Đã bật ghi JPEG an toàn"},
    TranslationPair{L"Fast JPEG writes enabled", L"Đã bật ghi JPEG nhanh"},
    TranslationPair{L"PHOTO DETAILS", L"CHI TIẾT ẢNH"},
    TranslationPair{L"Scanning folder…", L"Đang quét thư mục…"},
    TranslationPair{L"Reading EXIF metadata…", L"Đang đọc metadata EXIF…"},
    TranslationPair{L"Unrated", L"Chưa xếp hạng"},
    TranslationPair{L"Unmarked", L"Chưa đánh dấu"},
    TranslationPair{L"Preview", L"Xem trước"},
    TranslationPair{L"PICK", L"CHỌN"},
    TranslationPair{L"REJECT", L"LOẠI"},
    TranslationPair{L"IMAGE", L"ẢNH"},
    TranslationPair{L"Format", L"Định dạng"},
    TranslationPair{L"Label", L"Nhãn"},
    TranslationPair{L"None", L"Không"},
    TranslationPair{L"Red", L"Đỏ"},
    TranslationPair{L"Yellow", L"Vàng"},
    TranslationPair{L"Green", L"Xanh lá"},
    TranslationPair{L"Blue", L"Xanh dương"},
    TranslationPair{L"Purple", L"Tím"},
    TranslationPair{L"Stars: None", L"Sao: Không"},
    TranslationPair{L"Pick state: Pick", L"Trạng thái chọn: Chọn"},
    TranslationPair{L"Pick state: Reject", L"Trạng thái chọn: Loại"},
    TranslationPair{L"Pick state: Unmarked", L"Trạng thái chọn: Chưa đánh dấu"},
    TranslationPair{L"Zoom: Width", L"Thu phóng: Vừa chiều rộng"},
    TranslationPair{L"Zoom: Height", L"Thu phóng: Vừa chiều cao"},
    TranslationPair{L"Zoom: 100%", L"Thu phóng: 100%"},
    TranslationPair{L"Zoom: Fill", L"Thu phóng: Lấp đầy"},
    TranslationPair{L"Zoom: Custom", L"Thu phóng: Tùy chỉnh"},
    TranslationPair{L"Select 2 to 6 photos to enter Compare.", L"Chọn từ 2 đến 6 ảnh để mở chế độ So sánh."},
    TranslationPair{L"This unusually large batch was completed without an in-memory Undo record to protect available RAM.", L"Lô ảnh rất lớn này đã hoàn tất mà không lưu bản ghi Hoàn tác trong RAM để bảo vệ bộ nhớ khả dụng."},
    TranslationPair{L"Future edits will be written inside that file format.", L"Các chỉnh sửa sau này sẽ được ghi bên trong định dạng tệp đó."},
    TranslationPair{L"Future edits will be written to XMP sidecars for that format.", L"Các chỉnh sửa sau này sẽ được ghi vào tệp XMP sidecar cho định dạng đó."},
    TranslationPair{L"QuickSift encountered an unexpected startup or runtime failure.", L"QuickSift gặp lỗi bất ngờ khi khởi động hoặc đang chạy."},
    TranslationPair{L"File conflict — nothing changed", L"Xung đột tệp — không có gì thay đổi"},
    TranslationPair{L"Low storage — nothing changed", L"Dung lượng lưu trữ thấp — không có gì thay đổi"},
    TranslationPair{L"Storage check failed — nothing changed", L"Kiểm tra dung lượng thất bại — không có gì thay đổi"},
    TranslationPair{L"Undo stopped before changing files", L"Hoàn tác đã dừng trước khi thay đổi tệp"},
    TranslationPair{L"Undo stopped for safety", L"Hoàn tác đã dừng để đảm bảo an toàn"},
    TranslationPair{L"Redo stopped before changing files", L"Làm lại đã dừng trước khi thay đổi tệp"},
    TranslationPair{L"Redo stopped for safety", L"Làm lại đã dừng để đảm bảo an toàn"},
};

// Longer fragments come first. This second pass localizes generated status,
// EXIF, and safety messages whose counts, filenames, or byte values make exact
// table lookup impossible. User data is otherwise preserved verbatim.
constexpr std::array kDynamicFragments{
    TranslationPair{L"No additional EXIF fields were exposed by the installed codec.", L"Codec đã cài đặt không cung cấp thêm trường EXIF nào."},
    TranslationPair{L"QuickSift stopped immediately and did not overwrite or rename anything.", L"QuickSift đã dừng ngay và không ghi đè hoặc đổi tên bất kỳ tệp nào."},
    TranslationPair{L"modified-date filter active", L"đang bật bộ lọc ngày chỉnh sửa"},
    TranslationPair{L"synchronized wheel zoom and drag pan", L"đồng bộ thu phóng bằng con lăn và kéo để di chuyển"},
    TranslationPair{L"click a pane, then 1–5 to rate", L"nhấp một khung rồi bấm 1–5 để xếp hạng"},
    TranslationPair{L"Enter: open selection", L"Enter: mở vùng chọn"},
    TranslationPair{L"Ctrl+A: select all", L"Ctrl+A: chọn tất cả"},
    TranslationPair{L"1–5: rate selection", L"1–5: xếp hạng vùng chọn"},
    TranslationPair{L"arrows: next/previous", L"mũi tên: ảnh sau/trước"},
    TranslationPair{L"wheel: zoom", L"con lăn: thu phóng"},
    TranslationPair{L"1–5: rate", L"1–5: xếp hạng"},
    TranslationPair{L"Esc: thumbnails", L"Esc: ảnh thu nhỏ"},
    TranslationPair{L"reading ratings", L"đang đọc xếp hạng"},
    TranslationPair{L" selected", L" đã chọn"},
    TranslationPair{L" photos", L" ảnh"},
    TranslationPair{L" photo", L" ảnh"},
    TranslationPair{L"Taken:", L"Ngày chụp:"},
    TranslationPair{L"Dimensions:", L"Kích thước:"},
    TranslationPair{L"Camera maker:", L"Hãng máy ảnh:"},
    TranslationPair{L"Camera:", L"Máy ảnh:"},
    TranslationPair{L"Lens:", L"Ống kính:"},
    TranslationPair{L"Exposure:", L"Tốc độ màn trập:"},
    TranslationPair{L"Aperture:", L"Khẩu độ:"},
    TranslationPair{L"Focal length:", L"Tiêu cự:"},
    TranslationPair{L"Exposure bias:", L"Bù sáng:"},
    TranslationPair{L"White balance:", L"Cân bằng trắng:"},
    TranslationPair{L"Bit depth:", L"Độ sâu bit:"},
    TranslationPair{L"Color space:", L"Không gian màu:"},
    TranslationPair{L"File size:", L"Kích thước tệp:"},
    TranslationPair{L"Format:", L"Định dạng:"},
    TranslationPair{L"Flash:", L"Đèn flash:"},
    TranslationPair{L"Unknown", L"Không rõ"},
    TranslationPair{L" [truncated]", L" [đã rút gọn]"},
    TranslationPair{L"[EXIF text truncated]", L"[Nội dung EXIF đã được rút gọn]"},
    TranslationPair{L"Destination conflict", L"Xung đột đích đến"},
    TranslationPair{L"Destination unavailable", L"Đích đến không khả dụng"},
    TranslationPair{L"Deleted folder conflict", L"Xung đột thư mục đã xóa"},
    TranslationPair{L"Deleted folder unavailable", L"Thư mục đã xóa không khả dụng"},
    TranslationPair{L"nothing changed", L"không có gì thay đổi"},
    TranslationPair{L"Low storage", L"Dung lượng lưu trữ thấp"},
    TranslationPair{L"Storage check failed", L"Kiểm tra dung lượng thất bại"},
    TranslationPair{L"File conflict", L"Xung đột tệp"},
    TranslationPair{L"File operation stopped", L"Thao tác tệp đã dừng"},
    TranslationPair{L"Metadata update stopped", L"Cập nhật metadata đã dừng"},
    TranslationPair{L"Rating update incomplete", L"Cập nhật xếp hạng chưa hoàn tất"},
    TranslationPair{L"Pick/reject update incomplete", L"Cập nhật chọn/loại chưa hoàn tất"},
    TranslationPair{L"Color-label update incomplete", L"Cập nhật nhãn màu chưa hoàn tất"},
    TranslationPair{L"Undo stopped", L"Hoàn tác đã dừng"},
    TranslationPair{L"Redo stopped", L"Làm lại đã dừng"},
    TranslationPair{L"before changing files", L"trước khi thay đổi tệp"},
    TranslationPair{L"for safety", L"để đảm bảo an toàn"},
    TranslationPair{L"Direct RAW metadata enabled", L"Đã bật metadata RAW trực tiếp"},
    TranslationPair{L"Inside file", L"Bên trong tệp"},
    TranslationPair{L"XMP sidecar", L"XMP sidecar"},
    TranslationPair{L"advanced", L"nâng cao"},
    TranslationPair{L"verified copy", L"bản sao đã xác minh"},
    TranslationPair{L"faster", L"nhanh hơn"},
};

void ReplaceAll(std::wstring& value, std::wstring_view from, std::wstring_view to) {
    if (from.empty()) return;
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::wstring::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

} // namespace

std::wstring Localizer::Text(std::wstring_view english) const {
    if (language_ == Language::English) return std::wstring(english);
    for (const auto& item : kTranslations) {
        if (item.english == english) return std::wstring(item.vietnamese);
    }
    std::wstring localized(english);
    for (const auto& item : kDynamicFragments) ReplaceAll(localized, item.english, item.vietnamese);
    return localized;
}

std::wstring Localizer::WindowTitle() const {
    return Text(L"QuickSift — Fast Photo Shortlisting");
}

std::wstring Localizer::AboutDocument() const {
    if (language_ == Language::Vietnamese) {
        return std::wstring(L"Giới thiệu QuickSift\r\n\r\n") +
            L"QuickSift " QS_VERSION_WSTRING L"\r\n"
            L"Chọn lọc ảnh nhanh cho Windows\r\n"
            L"Bản quyền © 2026 Vo Hoang Tri Dung\r\n"
            L"GPL-2.0-or-later\r\n"
            L"https://quicksift.pages.dev/\r\n\r\n"
            L"CAM KẾT CUNG CẤP MÃ NGUỒN BẰNG VĂN BẢN\r\n\r\n"
            L"QuickSift được phân phối theo các điều khoản của Giấy phép Công cộng GNU, phiên bản 2 hoặc bất kỳ phiên bản nào mới hơn (GPL-2.0-or-later).\r\n\r\n"
            L"Trong thời hạn ít nhất ba năm kể từ ngày bản sao QuickSift này được phân phối, bạn có thể nhận được mã nguồn đầy đủ tương ứng của QuickSift, bao gồm mọi sửa đổi đối với các thành phần thuộc GPL được phân phối cùng với QuickSift, bằng cách gửi yêu cầu bằng văn bản tới:\r\n\r\n"
            L"Vo Hoang Tri Dung\r\n"
            L"Email: tridung.vo.1998@gmail.com\r\n\r\n"
            L"Vui lòng nêu rõ phiên bản QuickSift cụ thể mà bạn yêu cầu mã nguồn.\r\n"
            L"Mã nguồn sẽ được cung cấp trên một phương tiện lưu trữ vật lý bền vững hoặc bằng một phương thức phù hợp khác, miễn phí ngoại trừ chi phí hợp lý phát sinh trực tiếp từ việc cung cấp mã nguồn.\r\n\r\n"
            L"Cam kết bằng văn bản này được cung cấp nhằm đáp ứng các yêu cầu của Giấy phép Công cộng GNU, phiên bản 2 hoặc bất kỳ phiên bản nào mới hơn.\r\n"
            L"Toàn văn giấy phép được cung cấp cùng với QuickSift trong tệp LICENSE.txt.\r\n\r\n"
            L"MÃ NGUỒN TƯƠNG ỨNG\r\n\r\n"
            L"https://github.com/tridungvo1998/quicksift/";
    }

    return std::wstring(L"About QuickSift\r\n\r\n") +
        L"QuickSift " QS_VERSION_WSTRING L"\r\n"
        L"Fast Photo Culling for Windows\r\n"
        L"Copyright © 2026 Vo Hoang Tri Dung\r\n"
        L"GPL-2.0-or-later\r\n"
        L"https://quicksift.pages.dev/\r\n\r\n"
        L"WRITTEN OFFER FOR SOURCE CODE\r\n\r\n"
        L"QuickSift is distributed under the terms of the GNU General Public License, version 2 or any later version (GPL-2.0-or-later).\r\n\r\n"
        L"For a period of at least three years from the date of distribution of this copy of QuickSift, you may obtain the corresponding complete source code for QuickSift, including any modifications to GPL-covered components distributed with QuickSift, by sending a written request to:\r\n\r\n"
        L"Vo Hoang Tri Dung\r\n"
        L"Email: tridung.vo.1998@gmail.com\r\n\r\n"
        L"Please identify the specific version of QuickSift for which you are requesting the source code.\r\n"
        L"The source code will be provided on a durable physical medium or by another appropriate means, at no charge other than the reasonable cost of physically performing the source-code distribution.\r\n\r\n"
        L"This written offer is provided in accordance with the requirements of the GNU General Public License, version 2 or any later version.\r\n"
        L"The complete license text is included with QuickSift in the file LICENSE.txt.\r\n\r\n"
        L"CORRESPONDING SOURCE CODE\r\n\r\n"
        L"https://github.com/tridungvo1998/quicksift/";
}

std::wstring Localizer::HelpDocument() const {
    if (language_ == Language::Vietnamese) {
        return std::wstring(L"QuickSift " QS_VERSION_WSTRING L"\n") + LR"HELP(Vo Hoang Tri Dung
https://quicksift.pages.dev/
GPL-2.0-or-later (Xin xem Giới thiệu QuickSift để biết cam kết cung cấp mã nguồn)

QuickSift — Hướng dẫn dễ hiểu

PHÍM TẮT
• Điều hướng: Phím mũi tên (di chuyển), Home/End (đến đầu/cuối), Esc (về ảnh thu nhỏ), Enter (mở ảnh đã chọn).
• Xếp hạng: 0 (xóa), 1–5 (xếp hạng).
• Chọn/Loại: P (chọn), X (loại), U (bỏ đánh dấu).
• Nhãn màu: 6 (Đỏ), 7 (Vàng), 8 (Xanh lá), 9 (Xanh dương), Ctrl+5 (Tím), Ctrl+0 (xóa nhãn).
• Xem ảnh: Con lăn (thu phóng), Kéo (di chuyển), F (vừa khung), Z (100%), R (xoay), F11 (toàn màn hình), F7 (thanh phim).
• Lịch sử: Ctrl+Z (hoàn tác), Ctrl+Y (làm lại).

BẮT ĐẦU
• Mở: chọn thư mục chứa ảnh. Danh sách thư mục ở mép trái cho phép chuyển nhanh sang thư mục con.
• Ảnh thu nhỏ: xem toàn bộ ảnh. Nhấp để chọn; Ctrl+A chọn tất cả; Shift+nhấp chọn một dải.
• Một ảnh: xem ảnh đang chọn ở kích thước lớn.
• So sánh: mở từ hai ảnh trở lên cạnh nhau. Phím mũi tên chỉ chuyển tiêu điểm giữa các ảnh đang so sánh.

CHỌN VÀ XẾP HẠNG
• Phím 1–5 hoặc các nút sao: đặt xếp hạng.
• 0: xóa xếp hạng.
• Pick / Reject / Unmark: đánh dấu ảnh chọn, ảnh loại hoặc bỏ đánh dấu.
• Label: gán nhãn màu. Phím 6–9 gán Đỏ, Vàng, Xanh lá và Xanh dương; Ctrl+5 gán Tím; Ctrl+0 xóa nhãn.
• Các thao tác áp dụng cho toàn bộ ảnh đang chọn trong chế độ ảnh thu nhỏ.

XEM ẢNH
• Con lăn chuột: phóng to/thu nhỏ.
• Kéo chuột: di chuyển vùng nhìn khi ảnh đã phóng to.
• Zoom: chọn Vừa khung, Vừa chiều rộng, Vừa chiều cao, 100% hoặc Lấp đầy.
• Sync: trong chế độ so sánh, đồng bộ thu phóng và vị trí giữa các ảnh.
• Face Lock: khi có khuôn mặt, tự căn vùng nhìn vào khuôn mặt lớn nhất khi đổi ảnh hoặc thu phóng.
• Toàn màn hình / F11: lấp đầy màn hình và thu thanh tiêu đề vào mép trên. Giữ con trỏ ở vùng mép trên trong chốc lát để hiện thanh. Thanh tiêu đề nổi trên ảnh thay vì làm vùng xem ảnh đổi kích thước; đưa chuột ra xa để thanh tự ẩn lại.
• Viền đỏ quanh ảnh thu nhỏ: khuôn mặt lớn nhất có vẻ khá nhòe hoặc lệch nét. Di chuột lên viền để xem giải thích. Ngưỡng trung bình được đặt khá thận trọng và chỉ đánh dấu các trường hợp hụt nét rõ ràng; nên kiểm tra ở 100% trước khi loại ảnh.
• Xoay trái / phải: xoay cách hiển thị; tệp gốc không bị thay đổi.

LỌC VÀ SẮP XẾP
• Stars, Pick state, Labels, Format và Date chỉ thay đổi những ảnh được hiển thị; không xóa tệp.
• Vùng chọn được giới hạn có chủ ý trong kết quả đang hiển thị. Khi bộ lọc ẩn một ảnh đã chọn, ảnh đó được bỏ khỏi vùng chọn để không trở thành mục tiêu lệnh vô hình.
• Bộ lọc metadata chỉ nhận giá trị đã đọc chắc chắn; ảnh đang chờ đọc, không đọc được hoặc có giá trị tùy chỉnh không bị giả vờ là chưa xếp hạng/chưa đánh dấu/chưa gắn nhãn.
• Sort thay đổi thứ tự hiển thị theo tên, ngày, xếp hạng hoặc định dạng.
• Thumbnail size thay đổi kích thước lưới.

QUẢN LÝ TỆP
• Copy: sao chép ảnh đã chọn sang thư mục khác và hiện thông báo hoàn tất trong 2 giây.
• Move: di chuyển ảnh đã chọn sau khi xác minh đích và hiện thông báo hoàn tất trong 2 giây.
• Tùy chọn “Sao chép/Di chuyển RAW cùng JPG” mặc định tắt. Khi bật, thao tác trên JPG cũng áp dụng cho mọi tệp RAW có cùng tên gốc; các định dạng không phải RAW có cùng tên không bị ảnh hưởng. Nếu bất kỳ JPG nào thiếu RAW tương ứng, QuickSift chỉ hiện một cảnh báo cho cả lô. Nếu nhiều định dạng RAW dùng chung tên gốc, tất cả đều được xử lý và XMP dùng chung chỉ được chuyển một lần.
• Delete: mở hộp thoại xác nhận, sau đó chuyển ảnh đã chọn và XMP sidecar tương ứng vào thư mục con `Deleted` của thư mục đang duyệt. Thao tác này không dùng Thùng rác.
• Undo / Redo: hoàn tác hoặc làm lại thao tác metadata và thao tác tệp còn được bảo vệ trong lịch sử.
• QuickSift không tự ghi đè và không tự tạo tên kiểu “(2)” khi có xung đột.

THÔNG TIN VÀ CÀI ĐẶT
• INFO hiển thị EXIF của ảnh đang hoạt động.
• Settings điều khiển huy hiệu, giao diện, ngôn ngữ và cách ghi metadata.
• Đổi ngôn ngữ cập nhật ngay mọi nhãn, tiêu đề, trạng thái và phần nội dung đang mở.


TÀI LIỆU KỸ THUẬT

MÔ HÌNH DỮ LIỆU
• allPhotos là danh mục sở hữu duy nhất. Lớp hiển thị chỉ lưu chỉ số ổn định vào danh mục này.
• Bộ lọc và sắp xếp không sao chép PhotoItem; chúng sắp xếp chỉ số.
• Dấu vân tay bộ nhớ đệm gồm đường dẫn chuẩn hóa, kích thước, thời gian sửa và dấu thời gian sidecar.

GIẢI MÃ VÀ LẬP LỊCH
• Công việc được chia thành Interactive, Visible, Face và Idle.
• Số worker, số luồng codec, ngân sách buffer, mức đọc trước và độ sâu cache được suy ra từ RAM, CPU, GPU và loại ổ đĩa.
• Yêu cầu lỗi thời bị hủy bằng generation/navigation epoch. Yêu cầu cùng nguồn được hợp nhất; chất lượng cao hơn có thể phục vụ các mức thấp hơn.
• Cảnh báo nét khuôn mặt kết hợp phương sai Laplacian với năng lượng gradient trên vùng khuôn mặt lớn nhất. Ngưỡng trung bình chỉ đánh dấu khi mất chi tiết tần số cao rất rõ hoặc cả hai phép đo đều cho thấy lệch nét.
• JPEG dùng đường TurboJPEG khi có lợi; WIC là đường tương thích. RAW ưu tiên preview nhúng trước khi demosaic. AVIF dùng libavif/dav1d với libyuv.
• WIC native transforms, thumbnail nhúng, progressive refinement và Direct2D image source theo nhu cầu được dùng khi codec/driver hỗ trợ.

BỘ NHỚ
• Mọi decode giữ reservation cho tới khi chủ sở hữu pixel cuối cùng giải phóng.
• Cache RAM dùng admission chống quét: mục mới ở vùng thử nghiệm; mục được truy cập lại chuyển sang vùng bảo vệ. Prefetch nhàn rỗi không được đẩy ảnh tương tác ra khỏi cache.
• Sự kiện bộ nhớ thấp của Windows, ngân sách DXGI và áp lực GPU có thể hạ giới hạn, hủy prefetch, trim image source và nhả buffer pool.
• Luồng nền dùng memory priority thấp và EcoQoS; luồng tương tác giữ ưu tiên bình thường.
• Buffer tái sử dụng có giới hạn; vùng lớn nhàn rỗi có thể được decommit/offer cho Windows thay vì bị giữ vô thời hạn.
• Sau khi rời So sánh, bitmap độ phân giải cao, tile, image source và phiên decoder không còn hoạt động được giải phóng sau một khoảng nhàn rỗi ngắn. Chế độ Một ảnh giữ ảnh hiện tại; máy nhiều RAM có thể giữ thêm ảnh lân cận. Windows có thể tạm giữ các trang đã giải phóng trong working set cho tới khi hệ thống cần RAM.

ĐỒ HỌA
• Trên Windows 10+ với driver phù hợp, ảnh WIC lớn dùng ID2D1ImageSourceFromWic theo nhu cầu. Direct2D có thể giữ JPEG ở dạng luminance/chrominance và chỉ nạp tile cần thiết.
• Nếu đường hiện đại không khả dụng, ứng dụng quay về bitmap BGRA và tile WIC.
• Chất lượng hiển thị chỉ tăng: preview, thumbnail hoặc tile đến muộn không được thay thế nguồn sắc nét hơn đang hiển thị. Bitmap nền và tile dùng cùng tọa độ nguồn và tâm pan đã giới hạn.
• Khi thu nhỏ hoặc thiếu bộ nhớ, tài nguyên đồ họa ngoài vùng nhìn được trim/offer; DXGI Trim chỉ chạy ở ranh giới nhàn rỗi thực sự.

I/O VÀ BỘ NHỚ ĐỆM Ổ ĐĨA
• Quét thư mục dùng FindFirstFileExW và LARGE_FETCH khi phù hợp.
• Đọc tuần tự dùng hint tuần tự; phiên pan/tile dùng hint ngẫu nhiên.
• Tệp nén có thể được memory-map và prefetch có giới hạn; số lượt đọc trước phụ thuộc loại ổ đĩa và RAM.
• Metadata dùng SQLite WAL với page-cache và mmap giới hạn. Bitmap/tile vẫn là tệp riêng để tránh làm phình database.
• Cache đĩa có giới hạn dung lượng, giới hạn khoảng trống, tuổi thọ, LRU gần đúng và dọn tệp mồ côi/tạm.
• Pyramid nhiều độ phân giải chỉ được tạo cho ảnh rất lớn hoặc ảnh được xem lặp lại, và luôn nằm trong ngân sách ổ đĩa.

AN TOÀN METADATA VÀ TỆP
• XMP sidecar là đường an toàn mặc định cho các định dạng có rủi ro ghi trực tiếp.
• Chế độ ghi an toàn tạo bản tạm, xác minh, flush và thay thế nguyên tử khi hệ thống tệp cho phép.
• Copy/Move/Delete kiểm tra xung đột và thay đổi danh tính tệp; khi có điều kiện bất thường, thao tác dừng thay vì đoán.

CAM KẾT CUNG CẤP MÃ NGUỒN

Chi tiết cam kết cung cấp mã nguồn bằng văn bản, thông tin liên hệ và mã nguồn tương ứng được trình bày trong mục Giới thiệu QuickSift.
)HELP";
    }

    return std::wstring(L"QuickSift " QS_VERSION_WSTRING L"\n") + LR"HELP(Vo Hoang Tri Dung
https://quicksift.pages.dev/
GPL-2.0-or-later (See About QuickSift for the written offer for source code)

QuickSift — Plain-language Help

GETTING STARTED
• Open: choose a folder containing photos. The folder strip on the left lets you move into subfolders quickly.
• Thumbnails: see the whole shoot. Click to select; Ctrl+A selects all; Shift-click selects a range.
• Single: view the active photo large.
• Compare: place two or more selected photos side by side. Arrow keys move focus only among the photos already being compared.

CULLING AND RATING
• Keys 1–5 or the star buttons set a rating.
• 0 clears the rating.
• Pick / Reject / Unmark marks a keeper, a reject, or clears the cull mark.
• Label assigns a color label. Keys 6–9 assign Red, Yellow, Green, and Blue; Ctrl+5 assigns Purple; Ctrl+0 clears the label.
• In thumbnail view, these actions apply to the whole current selection.

VIEWING
• Mouse wheel: zoom in or out.
• Drag: pan when the photo is zoomed.
• Zoom: choose Fit, Fit width, Fit height, 100%, or Fill.
• Sync: in Compare view, keeps zoom and pan aligned between panes.
• Face Lock: when a face is available, recenters on the largest face when changing photos or zooming.
• Fullscreen / F11 fills the monitor and retracts the title bar into the top edge. Rest the pointer briefly against the top edge to reveal it. The bar floats over the photograph without resizing the image view; move away and it hides again.
• Red thumbnail border: the largest detected face appears quite blurry or out of focus. Hover the border for the explanation. The medium threshold is intentionally conservative and flags only clear misses; verify at 100% before rejecting the photo.
• Rotate left / right changes only the display orientation; it does not rotate the original file.

FILTERING AND SORTING
• Stars, Pick state, Labels, Format, and Date change what is visible; they do not delete files.
• Selection is intentionally limited to visible results. If a filter hides a selected photo, it is removed from selection so it cannot remain an invisible command target.
• Metadata filters accept only authoritative values; pending, unreadable, and unsupported custom values do not masquerade as unrated, unmarked, or unlabeled.
• Sort changes display order by name, date, rating, or format.
• Thumbnail size changes the grid density.

FILE MANAGEMENT
• Copy copies selected photos to another folder and shows a two-second completion toast.
• Move moves selected photos after verifying the destination and shows a two-second completion toast.
• “Copy/Move RAW with JPG” is off by default. When enabled, operating on a JPG also includes every RAW file with the same base name; same-name non-RAW formats are unaffected. If any selected JPG has no corresponding RAW, QuickSift shows one warning for the whole batch. Multiple RAW formats sharing the same base name are all included; a shared XMP sidecar is transferred only once.
• Delete asks for confirmation, then moves selected photos and matching XMP sidecars into a `Deleted` subfolder inside the folder currently being browsed. It does not use the Recycle Bin.
• Undo / Redo replays protected metadata and file operations that remain in history.
• QuickSift never silently overwrites and never invents a “(2)” filename on conflict.

INFO AND SETTINGS
• INFO shows EXIF for the active photo.
• Settings controls badges, appearance, language, and metadata-writing behavior.
• Changing language immediately refreshes every caption, title, status string, and open documentation surface.


TECHNICAL DOCUMENTATION

DATA MODEL
• allPhotos is the single owning catalog. The visible layer stores stable indices into that catalog.
• Filtering and sorting move indices rather than copying PhotoItem records.
• Persistent-cache fingerprints include normalized path, file size, modification stamp, and sidecar stamp.

DECODING AND SCHEDULING
• Work is separated into Interactive, Visible, Face, and Idle lanes.
• Worker counts, codec threads, buffer budgets, read-ahead, and cache depth are derived from RAM, CPU topology, GPU budget, and storage class.
• Obsolete work is cancelled through generation/navigation epochs. Requests for the same source are coalesced; a higher-quality decode may satisfy lower levels.
• The face-focus warning combines Laplacian variance with broad gradient energy over the largest face crop. At the medium threshold, a face is flagged only after severe high-frequency loss or when both measurements indicate clear defocus.
• JPEG uses TurboJPEG when its measured path is advantageous, with WIC as compatibility fallback. RAW prefers embedded previews before demosaic. AVIF uses libavif/dav1d with libyuv.
• WIC native transforms, embedded thumbnails, progressive refinement, and on-demand Direct2D image sources are used when the codec and driver support them.

MEMORY
• Every decode retains its reservation until the final pixel owner releases it.
• The RAM cache uses scan-resistant admission: new entries begin in probation; repeatedly used entries graduate to protected storage. Idle prefetch cannot evict interactively reused images.
• Windows low-memory events, DXGI budgets, and GPU pressure can lower limits, cancel prefetch, trim image sources, and release buffer pools.
• Background threads use low memory priority and EcoQoS; interactive work remains normal priority.
• Reusable buffers are bounded; large idle regions can be decommitted or offered to Windows rather than retained indefinitely.
• After Compare ends, inactive high-resolution bitmaps, tiles, image sources, and decoder sessions are released after a short idle delay. Single view keeps the active photo; higher-RAM systems may retain nearby photos. Windows may temporarily keep released pages in the process working set until the system needs them.

GRAPHICS
• On Windows 10+ with a suitable driver, large WIC images use on-demand ID2D1ImageSourceFromWic. Direct2D can retain JPEG luminance/chrominance planes and populate only required tiles.
• If the modern path is unavailable, the application falls back to BGRA bitmaps and WIC tiles.
• Display quality only moves upward: late previews, thumbnails, or tiles cannot replace a sharper source already on screen. The base bitmap and tile layer use the same source coordinates and clamped pan centre.
• When minimized or under pressure, off-viewport graphics are trimmed/offered; DXGI Trim runs only at genuine idle boundaries.

I/O AND DISK CACHE
• Directory scans use FindFirstFileExW and LARGE_FETCH when appropriate.
• Whole-file reads use sequential hints; retained pan/tile sessions use random-access hints.
• Encoded sources may be memory-mapped with bounded prefetch; read-ahead depth follows storage and RAM characteristics.
• Metadata uses SQLite WAL with bounded page-cache and mmap settings. Bitmap/tile payloads remain separate files to avoid database bloat.
• Disk cache management enforces capacity, free-space reserve, age, approximate LRU, and orphan/temporary cleanup.
• Multi-resolution pyramids are created only for very large or repeatedly inspected images and remain under the disk budget.

METADATA AND FILE SAFETY
• XMP sidecars remain the safe default for formats where direct writing is risky.
• Safe-write mode creates a temporary copy, validates it, flushes it, and atomically replaces the original when the filesystem supports that sequence.
• Copy/Move/Delete check conflicts and file-identity changes; unexpected conditions stop the operation rather than guessing.

WRITTEN OFFER FOR SOURCE CODE

The written offer, contact information, license details, and corresponding source-code location are provided in About QuickSift.
)HELP";
}

} // namespace quicksift
