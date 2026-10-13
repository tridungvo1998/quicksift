// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Authoritative localized string tables and lookup behavior.

#include "localization.h"
#include "version.h"

#include <array>
#include <initializer_list>
#include <algorithm>
#include <utility>
#include <unordered_map>
#include <vector>

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
    TranslationPair{L"Couldn't load image", L"Không thể tải ảnh"},
    TranslationPair{L"Red border: at least one detected face looks blurred (out of focus or motion blur) or has its eyes closed. The checks are deliberately conservative; treat this as a warning, not a definitive test.",
        L"Viền đỏ: ít nhất một khuôn mặt được phát hiện có vẻ bị nhòe (lệch nét hoặc nhòe chuyển động) hoặc đang nhắm mắt. Các phép kiểm tra được đặt thận trọng; đây là cảnh báo, không phải phép kiểm tra tuyệt đối."},
    TranslationPair{L"Face Lock centers the view on the face closest to the image centre, or on the face you click. Detection runs locally through Windows Media FaceAnalysis.",
        L"Khóa khuôn mặt căn vùng nhìn vào khuôn mặt gần tâm ảnh nhất, hoặc vào khuôn mặt bạn nhấp chọn. Việc phát hiện chạy cục bộ qua Windows Media FaceAnalysis."},
    TranslationPair{L"Face", L"Khuôn mặt"},
    TranslationPair{L"out of focus", L"lệch nét"},
    TranslationPair{L"motion blur", L"nhòe chuyển động"},
    TranslationPair{L"eyes closed", L"nhắm mắt"},
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
    // 0.9.4.0 UI polish: left-pane section headers, empty states, diagnostic export.
    TranslationPair{L"USER COMMENTS", L"BÌNH LUẬN NGƯỜI DÙNG"},
    TranslationPair{L"THUMBNAILS", L"ẢNH THU NHỎ"},
    TranslationPair{L"RAW HANDLING", L"XỬ LÝ RAW"},
    TranslationPair{L"METADATA HANDLING", L"XỬ LÝ METADATA"},
    TranslationPair{L"HELP & DIAGNOSTICS", L"TRỢ GIÚP & CHẨN ĐOÁN"},
    TranslationPair{L"LANGUAGE", L"NGÔN NGỮ"},
    TranslationPair{L"No item in Library", L"Thư viện trống"},
    TranslationPair{L"Diagnostic log exported to:", L"Đã xuất nhật ký chẩn đoán tới:"},
    TranslationPair{L"QuickSift could not export the diagnostic log to that location.",
        L"QuickSift không thể xuất nhật ký chẩn đoán tới vị trí đó."},
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
    // 0.9.4.0 polish: previously untranslated UI, write-lane and error-catalog keys.
    TranslationPair{L"0 \u2605 (Unrated)", L"0 \u2605 (Chưa xếp hạng)"},
    TranslationPair{L"Select 2 to 6 photos to compare",
        L"Chọn từ 2 đến 6 ảnh để so sánh"},
    TranslationPair{L"Compare layout is unavailable for this window",
        L"Bố cục So sánh không khả dụng với kích thước cửa sổ này"},
    TranslationPair{L"EXIF could not be read because the decoder or filesystem failed temporarily.",
        L"Không thể đọc EXIF vì bộ giải mã hoặc hệ thống tệp tạm thời gặp lỗi."},
    TranslationPair{L"No EXIF information was exposed for this file.",
        L"Tệp này không cung cấp thông tin EXIF nào."},
    TranslationPair{L"Folder batch needs a refresh",
        L"Lô thư mục cần được làm mới"},
    TranslationPair{L"The photos were committed safely, but QuickSift could not finish scheduling all follow-up work. The final folder rebuild will restore ordering and metadata requests.",
        L"Các ảnh đã được ghi nhận an toàn, nhưng QuickSift chưa thể lên lịch hết các công việc tiếp theo. Lần dựng lại thư mục cuối cùng sẽ khôi phục thứ tự và các yêu cầu đọc metadata."},
    TranslationPair{L"Folder scan batch could not be committed",
        L"Không thể ghi nhận lô quét thư mục"},
    TranslationPair{L"QuickSift left the existing catalog unchanged because one incoming batch could not be validated or allocated safely.",
        L"QuickSift giữ nguyên danh mục hiện có vì một lô dữ liệu mới không thể được kiểm tra hoặc cấp phát an toàn."},
    TranslationPair{L"The photos were committed safely, but follow-up UI work stopped unexpectedly. The final folder rebuild will reconcile the view.",
        L"Các ảnh đã được ghi nhận an toàn, nhưng công việc giao diện tiếp theo đã dừng bất ngờ. Lần dựng lại thư mục cuối cùng sẽ đồng bộ lại chế độ xem."},
    TranslationPair{L"QuickSift left the existing catalog unchanged after an unexpected catalog error.",
        L"QuickSift giữ nguyên danh mục hiện có sau một lỗi danh mục không mong muốn."},
    TranslationPair{L"Folder scan failed",
        L"Quét thư mục thất bại"},
    TranslationPair{L"Folder scan incomplete",
        L"Quét thư mục chưa hoàn tất"},
    TranslationPair{L"FILE",
        L"TỆP"},
    TranslationPair{L"LIBRARY",
        L"THƯ VIỆN"},
    TranslationPair{L"SETTINGS",
        L"CÀI ĐẶT"},
    TranslationPair{L"User comment saved.",
        L"Đã lưu ghi chú người dùng."},
    TranslationPair{L"QuickSift could not locate one or more metadata targets.",
        L"QuickSift không tìm thấy một hoặc nhiều tệp cần cập nhật metadata."},
    TranslationPair{L"QuickSift could not determine an image size before preparing a verified metadata update. Nothing was changed.",
        L"QuickSift không xác định được kích thước ảnh trước khi chuẩn bị bản cập nhật metadata có xác minh. Không có gì thay đổi."},
    TranslationPair{L"{operation} needs cumulative free space for the complete batch.",
        L"{operation} cần đủ dung lượng trống cộng dồn cho toàn bộ lô."},
    TranslationPair{L"Volume:",
        L"Ổ đĩa:"},
    TranslationPair{L"Required:",
        L"Cần:"},
    TranslationPair{L"Available:",
        L"Còn trống:"},
    TranslationPair{L"Nothing was changed.",
        L"Không có gì thay đổi."},
    TranslationPair{L"Metadata update is waiting",
        L"Cập nhật metadata đang chờ"},
    TranslationPair{L"QuickSift serializes verified file and metadata transactions so they cannot race over the same media.",
        L"QuickSift xử lý tuần tự các giao dịch tệp và metadata có xác minh để chúng không tranh chấp trên cùng một tệp."},
    TranslationPair{L"{label} update stopped",
        L"{label}: cập nhật đã dừng"},
    TranslationPair{L"{label} already matches the selected photos — no files were rewritten",
        L"{label} đã khớp với các ảnh được chọn — không tệp nào bị ghi lại"},
    TranslationPair{L"{label} update could not start",
        L"{label}: không thể bắt đầu cập nhật"},
    TranslationPair{L"Another serialized transaction became active before the metadata batch could be submitted.",
        L"Một giao dịch tuần tự khác đã bắt đầu trước khi lô metadata kịp được gửi."},
    TranslationPair{L"{count} files",
        L"{count} tệp"},
    TranslationPair{L"Retry",
        L"Thử lại"},
    TranslationPair{L"Skip",
        L"Bỏ qua"},
    TranslationPair{L"Some metadata items could not be written safely.",
        L"Một số mục metadata không thể được ghi an toàn."},
    TranslationPair{L"Total failed items:",
        L"Tổng số mục thất bại:"},
    TranslationPair{L"External-change conflicts:",
        L"Xung đột do thay đổi bên ngoài:"},
    TranslationPair{L"Edits made while this update was running were also undone:",
        L"Các chỉnh sửa thực hiện trong lúc cập nhật này đang chạy cũng đã được hoàn tác:"},
    TranslationPair{L"Metadata update cancelled",
        L"Đã hủy cập nhật metadata"},
    TranslationPair{L"Metadata update incomplete",
        L"Cập nhật metadata chưa hoàn tất"},
    TranslationPair{L"{label} update complete",
        L"{label}: đã cập nhật xong"},
    TranslationPair{L"Relinquish history",
        L"Bỏ lịch sử"},
    TranslationPair{L"Keep history",
        L"Giữ lịch sử"},
    TranslationPair{L"History reconcile is waiting",
        L"Đối chiếu lịch sử đang chờ"},
    TranslationPair{L"A serialized file or metadata transaction is still running. QuickSift will not drop history while that work is active.",
        L"Một giao dịch tệp hoặc metadata tuần tự vẫn đang chạy. QuickSift sẽ không bỏ lịch sử khi công việc đó còn hoạt động."},
    TranslationPair{L"History entry relinquished; on-disk files left untouched",
        L"Đã bỏ mục lịch sử; các tệp trên ổ đĩa được giữ nguyên"},
    TranslationPair{L"Metadata update blocked by an external change",
        L"Cập nhật metadata bị chặn do có thay đổi từ bên ngoài"},
    TranslationPair{L"Refresh from disk",
        L"Làm mới từ ổ đĩa"},
    TranslationPair{L"Keep catalog",
        L"Giữ danh mục"},
    TranslationPair{L"No catalog paths needed refresh; on-disk files left untouched",
        L"Không có đường dẫn nào trong danh mục cần làm mới; các tệp trên ổ đĩa được giữ nguyên"},
    TranslationPair{L"Catalog refreshed from disk; on-disk files left untouched",
        L"Đã làm mới danh mục từ ổ đĩa; các tệp trên ổ đĩa được giữ nguyên"},
    TranslationPair{L"Undo is waiting",
        L"Hoàn tác đang chờ"},
    TranslationPair{L"A serialized file or metadata transaction is still running. QuickSift will not let Undo race it.",
        L"Một giao dịch tệp hoặc metadata tuần tự vẫn đang chạy. QuickSift sẽ không để Hoàn tác chạy song song với nó."},
    TranslationPair{L"Undo history is incomplete",
        L"Lịch sử Hoàn tác không đầy đủ"},
    TranslationPair{L"The metadata history record is internally inconsistent, so QuickSift refused to write any file.",
        L"Bản ghi lịch sử metadata không nhất quán nên QuickSift từ chối ghi bất kỳ tệp nào."},
    TranslationPair{L"Undo could not start",
        L"Không thể bắt đầu Hoàn tác"},
    TranslationPair{L"Another serialized transaction became active before metadata Undo could be submitted.",
        L"Một giao dịch tuần tự khác đã bắt đầu trước khi lệnh Hoàn tác metadata kịp được gửi."},
    TranslationPair{L"Undoing metadata safely…",
        L"Đang hoàn tác metadata an toàn…"},
    TranslationPair{L"Another verified file transaction became active before Undo could be submitted.",
        L"Một giao dịch tệp có xác minh khác đã bắt đầu trước khi lệnh Hoàn tác kịp được gửi."},
    TranslationPair{L"Undoing and verifying file history…",
        L"Đang hoàn tác và xác minh lịch sử tệp…"},
    TranslationPair{L"Redo is waiting",
        L"Làm lại đang chờ"},
    TranslationPair{L"A serialized file or metadata transaction is still running. QuickSift will not let Redo race it.",
        L"Một giao dịch tệp hoặc metadata tuần tự vẫn đang chạy. QuickSift sẽ không để Làm lại chạy song song với nó."},
    TranslationPair{L"Redo history is incomplete",
        L"Lịch sử Làm lại không đầy đủ"},
    TranslationPair{L"Redo could not start",
        L"Không thể bắt đầu Làm lại"},
    TranslationPair{L"Another serialized transaction became active before metadata Redo could be submitted.",
        L"Một giao dịch tuần tự khác đã bắt đầu trước khi lệnh Làm lại metadata kịp được gửi."},
    TranslationPair{L"Redoing metadata safely…",
        L"Đang làm lại metadata an toàn…"},
    TranslationPair{L"Another verified file transaction became active before Redo could be submitted.",
        L"Một giao dịch tệp có xác minh khác đã bắt đầu trước khi lệnh Làm lại kịp được gửi."},
    TranslationPair{L"Redoing and verifying file history…",
        L"Đang làm lại và xác minh lịch sử tệp…"},
    TranslationPair{L"the file",
        L"tệp"},
    TranslationPair{L"Pick state: Mixed",
        L"Trạng thái chọn: Hỗn hợp"},
    TranslationPair{L"Performance HUD enabled.",
        L"Đã bật HUD hiệu năng."},
    TranslationPair{L"Performance HUD disabled.",
        L"Đã tắt HUD hiệu năng."},
    TranslationPair{L"JPEG metadata edits will use a verified temporary copy, post-write validation, and atomic replacement. This is slower, especially for large JPEGs or slow storage, but it protects the original from partial writes.",
        L"Chỉnh sửa metadata JPEG sẽ dùng một bản sao tạm có xác minh, kiểm tra sau khi ghi và thay thế nguyên tử. Cách này chậm hơn, nhất là với JPEG lớn hoặc ổ lưu trữ chậm, nhưng bảo vệ tệp gốc khỏi bị ghi dở dang."},
    TranslationPair{L"JPEG metadata will be written directly to the original file. This is faster, but a crash, power loss, storage fault, or metadata-library failure during the write can damage the JPEG. Enable Safe JPEG writes or use XMP sidecars when file protection matters more than speed.",
        L"Metadata JPEG sẽ được ghi trực tiếp vào tệp gốc. Cách này nhanh hơn, nhưng sự cố treo ứng dụng, mất điện, lỗi ổ lưu trữ hoặc lỗi thư viện metadata trong lúc ghi có thể làm hỏng tệp JPEG. Hãy bật Ghi JPEG an toàn hoặc dùng tệp XMP sidecar khi việc bảo vệ tệp quan trọng hơn tốc độ."},
    TranslationPair{L"QuickSift will update a verified temporary copy and atomically replace the original only after validation. Even so, proprietary RAW containers are less universally writable than JPEG, PNG, or TIFF. XMP sidecars remain the safer default. Existing metadata is not automatically migrated between storage modes.",
        L"QuickSift sẽ cập nhật một bản sao tạm có xác minh và chỉ thay thế nguyên tử tệp gốc sau khi kiểm tra. Dù vậy, định dạng RAW độc quyền khó ghi an toàn hơn JPEG, PNG hay TIFF. Tệp XMP sidecar vẫn là lựa chọn mặc định an toàn hơn. Metadata hiện có không được tự động chuyển giữa các chế độ lưu trữ."},
    TranslationPair{L"Safe JPG writes",
        L"Ghi JPG an toàn"},
    TranslationPair{L"RAW files",
        L"Tệp RAW"},
    TranslationPair{L"JPG, PNG and lossy formats",
        L"JPG, PNG và định dạng nén mất dữ liệu"},
    TranslationPair{L"Show performance HUD",
        L"Hiện HUD hiệu năng"},
    TranslationPair{L"Show diagnostic log",
        L"Hiện nhật ký chẩn đoán"},
    TranslationPair{L"All",
        L"Tất cả"},
    TranslationPair{L"formats",
        L"định dạng"},
    TranslationPair{L"A serialized transaction is still running",
        L"Một giao dịch tuần tự vẫn đang chạy"},
    TranslationPair{L"QuickSift runs file and metadata transactions serially to prevent races and accidental overwrites.",
        L"QuickSift chạy tuần tự các giao dịch tệp và metadata để tránh tranh chấp và ghi đè ngoài ý muốn."},
    TranslationPair{L"The destination path is not a normal folder. QuickSift stopped before changing any files.",
        L"Đường dẫn đích không phải là thư mục thông thường. QuickSift đã dừng trước khi thay đổi bất kỳ tệp nào."},
    TranslationPair{L"QuickSift could not safely inspect the destination folder. No files were changed.",
        L"QuickSift không thể kiểm tra an toàn thư mục đích. Không có tệp nào bị thay đổi."},
    TranslationPair{L"QuickSift could not determine the complete size of the selected files. It stopped before copying or moving anything.",
        L"QuickSift không xác định được tổng dung lượng của các tệp đã chọn. Ứng dụng đã dừng trước khi sao chép hoặc di chuyển bất kỳ tệp nào."},
    TranslationPair{L"QuickSift could not create or access the destination folder. No files were changed.",
        L"QuickSift không thể tạo hoặc truy cập thư mục đích. Không có tệp nào bị thay đổi."},
    TranslationPair{L"Another file operation is already running",
        L"Một thao tác tệp khác đang chạy"},
    TranslationPair{L"QuickSift serializes verified file transactions so two operations cannot race over the same media. The new request was not started.",
        L"QuickSift xử lý tuần tự các giao dịch tệp có xác minh để hai thao tác không tranh chấp trên cùng một tệp. Yêu cầu mới chưa được bắt đầu."},
    TranslationPair{L"Moving selected files to Deleted…",
        L"Đang chuyển các tệp đã chọn vào thư mục đã xóa…"},
    TranslationPair{L"Moving and verifying…",
        L"Đang di chuyển và xác minh…"},
    TranslationPair{L"Copying and verifying…",
        L"Đang sao chép và xác minh…"},
    TranslationPair{L"File Undo complete",
        L"Đã hoàn tác thao tác tệp"},
    TranslationPair{L"File Redo complete",
        L"Đã làm lại thao tác tệp"},
    TranslationPair{L"File operation cancelled",
        L"Đã hủy thao tác tệp"},
    TranslationPair{L"Copy completed with a warning",
        L"Sao chép hoàn tất kèm cảnh báo"},
    TranslationPair{L"Move completed with a warning",
        L"Di chuyển hoàn tất kèm cảnh báo"},
    TranslationPair{L"Rating: Unrated",
        L"Xếp hạng: Chưa xếp hạng"},
    TranslationPair{L"XMP vs embedded metadata for RAW",
        L"XMP hay metadata nhúng cho RAW"},
    TranslationPair{L"XMP vs embedded metadata for JPG/PNG/lossy",
        L"XMP hay metadata nhúng cho JPG/PNG/nén mất dữ liệu"},
    TranslationPair{L"XMP vs embedded metadata for TIFF",
        L"XMP hay metadata nhúng cho TIFF"},
    TranslationPair{L"Language",
        L"Ngôn ngữ"},
    TranslationPair{L"Waiting for a file",
        L"Đang chờ tệp"},
    TranslationPair{L"Waiting for {file} — in use by another app",
        L"Đang chờ {file} — tệp đang được ứng dụng khác sử dụng"},
    TranslationPair{L"File is in use by another app",
        L"Tệp đang được ứng dụng khác sử dụng"},
    TranslationPair{L"{file} stayed locked by another app for 30 seconds. The new value was not written and the previous value is shown again. Close the other app (or pause sync) and choose Retry, or choose Skip.",
        L"{file} bị ứng dụng khác khóa suốt 30 giây. Giá trị mới chưa được ghi và giá trị trước đó đã được hiển thị lại. Hãy đóng ứng dụng kia (hoặc tạm dừng đồng bộ) rồi chọn Thử lại, hoặc chọn Bỏ qua."},
    TranslationPair{L"Metadata update skipped",
        L"Đã bỏ qua cập nhật metadata"},
    TranslationPair{L"{file} was skipped because another app is using it. The previous value is shown again.",
        L"Đã bỏ qua {file} vì một ứng dụng khác đang sử dụng tệp. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"File changed outside QuickSift",
        L"Tệp đã thay đổi bên ngoài QuickSift"},
    TranslationPair{L"{file} changed outside QuickSift while the update was waiting. Nothing was overwritten.",
        L"{file} đã thay đổi bên ngoài QuickSift trong lúc bản cập nhật đang chờ. Không có gì bị ghi đè."},
    TranslationPair{L"There is not enough free space to update {file} safely. The previous value is shown again.",
        L"Không đủ dung lượng trống để cập nhật {file} an toàn. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"File is read-only",
        L"Tệp chỉ đọc"},
    TranslationPair{L"{file} is read-only (or on read-only media). The previous value is shown again.",
        L"{file} ở chế độ chỉ đọc (hoặc nằm trên thiết bị chỉ đọc). Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"Access denied",
        L"Bị từ chối truy cập"},
    TranslationPair{L"Windows denied write access to {file}. The previous value is shown again.",
        L"Windows từ chối quyền ghi vào {file}. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"Drive is not responding",
        L"Ổ đĩa không phản hồi"},
    TranslationPair{L"The drive holding {file} did not respond. The previous value is shown again.",
        L"Ổ đĩa chứa {file} không phản hồi. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"Metadata update could not be verified",
        L"Không thể xác minh bản cập nhật metadata"},
    TranslationPair{L"QuickSift could not verify the update of {file}; the original file was kept unchanged.",
        L"QuickSift không thể xác minh bản cập nhật của {file}; tệp gốc được giữ nguyên."},
    TranslationPair{L"Metadata cannot be stored in this file",
        L"Không thể lưu metadata vào tệp này"},
    TranslationPair{L"{file} cannot carry this metadata safely. The previous value is shown again.",
        L"{file} không thể lưu metadata này một cách an toàn. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"The requested value is outside the supported range. Nothing was changed.",
        L"Giá trị yêu cầu nằm ngoài phạm vi được hỗ trợ. Không có gì thay đổi."},
    TranslationPair{L"{file} could not be updated safely. The previous value is shown again.",
        L"Không thể cập nhật {file} an toàn. Giá trị trước đó đã được hiển thị lại."},
    TranslationPair{L"The metadata update was cancelled at a safe file boundary.",
        L"Cập nhật metadata đã bị hủy tại một ranh giới tệp an toàn."},
    TranslationPair{L"An image decoder did not release {file} in time. Try again in a moment.",
        L"Một bộ giải mã ảnh chưa giải phóng {file} kịp thời. Hãy thử lại sau giây lát."},
    TranslationPair{L"Metadata update queued",
        L"Cập nhật metadata đang xếp hàng"},
    TranslationPair{L"Saving after the current update…",
        L"Sẽ lưu sau bản cập nhật hiện tại…"},
    TranslationPair{L"Cache is busy",
        L"Bộ nhớ đệm đang bận"},
    TranslationPair{L"The thumbnail cache is busy; QuickSift will retry in the background.",
        L"Bộ nhớ đệm ảnh thu nhỏ đang bận; QuickSift sẽ thử lại ở chế độ nền."},
    TranslationPair{L"Cache was rebuilt",
        L"Đã dựng lại bộ nhớ đệm"},
    TranslationPair{L"The thumbnail cache could not be upgraded and was rebuilt. Thumbnails will be regenerated.",
        L"Không thể nâng cấp bộ nhớ đệm ảnh thu nhỏ nên bộ nhớ đệm đã được dựng lại. Ảnh thu nhỏ sẽ được tạo lại."},
    TranslationPair{L"Search is incomplete",
        L"Kết quả tìm kiếm chưa đầy đủ"},
    TranslationPair{L"Some photos could not be read for search and are matched by name only.",
        L"Một số ảnh không đọc được để tìm kiếm nên chỉ được so khớp theo tên."},
    TranslationPair{L"The ratings file could not be read.",
        L"Không thể đọc tệp xếp hạng."},
    TranslationPair{L"Ratings export failed",
        L"Xuất xếp hạng thất bại"},
    TranslationPair{L"The ratings file could not be written.",
        L"Không thể ghi tệp xếp hạng."},
    TranslationPair{L"The batch could not be prepared.",
        L"Không thể chuẩn bị lô xử lý."},
    TranslationPair{L"Background task stopped",
        L"Tác vụ nền đã dừng"},
    TranslationPair{L"A background task stopped unexpectedly. Details were written to the diagnostic log.",
        L"Một tác vụ nền đã dừng bất ngờ. Chi tiết đã được ghi vào nhật ký chẩn đoán."},
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
    // 0.9.4.0 Import / Export Ratings and face-outline UX.
    TranslationPair{L"Import / Export Ratings",
        L"Nhập / Xuất xếp hạng"},
    TranslationPair{L"Export saves the star rating, pick state and color tag of every file in this folder that has any of them to a UTF-8 text file. Import reads such a file and applies it to the files with the same names in this folder.",
        L"Xuất sẽ lưu số sao, trạng thái chọn và nhãn màu của mọi tệp trong thư mục này có ít nhất một trong các giá trị đó vào một tệp văn bản UTF-8. Nhập sẽ đọc tệp như vậy và áp dụng cho các tệp cùng tên trong thư mục này."},
    TranslationPair{L"Export…",
        L"Xuất…"},
    TranslationPair{L"Import…",
        L"Nhập…"},
    TranslationPair{L"Export ratings",
        L"Xuất xếp hạng"},
    TranslationPair{L"Import ratings",
        L"Nhập xếp hạng"},
    TranslationPair{L"Open a folder first.",
        L"Hãy mở một thư mục trước."},
    TranslationPair{L"QuickSift is still listing this folder. Try again when the folder has finished loading.",
        L"QuickSift vẫn đang liệt kê thư mục này. Hãy thử lại khi thư mục đã tải xong."},
    TranslationPair{L"Wait for the current file, metadata or ratings operation to finish, then try again.",
        L"Hãy chờ thao tác tệp, metadata hoặc xếp hạng hiện tại hoàn tất rồi thử lại."},
    TranslationPair{L"Text files (*.txt)",
        L"Tệp văn bản (*.txt)"},
    TranslationPair{L"QuickSift could not start the export.",
        L"QuickSift không thể bắt đầu xuất."},
    TranslationPair{L"Exporting ratings…",
        L"Đang xuất xếp hạng…"},
    TranslationPair{L"QuickSift could not start reading the file.",
        L"QuickSift không thể bắt đầu đọc tệp."},
    TranslationPair{L"Ratings exported: {count} of {total} files carry data — {file}",
        L"Đã xuất xếp hạng: {count} trên {total} tệp có dữ liệu — {file}"},
    TranslationPair{L"Export failed",
        L"Xuất thất bại"},
    TranslationPair{L"QuickSift could not write the ratings file. Nothing was exported.",
        L"QuickSift không thể ghi tệp xếp hạng. Không có gì được xuất."},
    TranslationPair{L"Ratings import cancelled because the folder changed.",
        L"Đã hủy nhập xếp hạng vì thư mục đã thay đổi."},
    TranslationPair{L"The file is far too large to be a QuickSift ratings file.",
        L"Tệp quá lớn để là một tệp xếp hạng QuickSift."},
    TranslationPair{L"Import failed",
        L"Nhập thất bại"},
    TranslationPair{L"QuickSift could not read the ratings file. Nothing was changed.",
        L"QuickSift không thể đọc tệp xếp hạng. Không có gì thay đổi."},
    TranslationPair{L"The file is empty.",
        L"Tệp trống."},
    TranslationPair{L"The file is not valid UTF-8 text.",
        L"Tệp không phải văn bản UTF-8 hợp lệ."},
    TranslationPair{L"The header line is missing or is not a QuickSift header.",
        L"Thiếu dòng tiêu đề hoặc đó không phải tiêu đề QuickSift."},
    TranslationPair{L"The \"Exported time\" line is missing or invalid.",
        L"Dòng \"Exported time\" bị thiếu hoặc không hợp lệ."},
    TranslationPair{L"The \"Total files count\" line is missing or invalid.",
        L"Dòng \"Total files count\" bị thiếu hoặc không hợp lệ."},
    TranslationPair{L"The rated/picked/tagged files count line is missing or invalid.",
        L"Dòng đếm số tệp đã xếp hạng/chọn/gắn nhãn bị thiếu hoặc không hợp lệ."},
    TranslationPair{L"The first separator line (—) is missing.",
        L"Thiếu dòng phân cách đầu tiên (—)."},
    TranslationPair{L"The \"Exported data:\" line is missing.",
        L"Thiếu dòng \"Exported data:\"."},
    TranslationPair{L"A data line is not \"FILENAME.EXTENSION | Star Rating | Pick State | Color Tag\" with valid values.",
        L"Một dòng dữ liệu không có dạng \"FILENAME.EXTENSION | Star Rating | Pick State | Color Tag\" với giá trị hợp lệ."},
    TranslationPair{L"The same file name is listed more than once.",
        L"Cùng một tên tệp xuất hiện nhiều lần."},
    TranslationPair{L"The closing separator line (—) is missing.",
        L"Thiếu dòng phân cách cuối (—)."},
    TranslationPair{L"The footer line is missing.",
        L"Thiếu dòng chân trang."},
    TranslationPair{L"There is unexpected text after the footer.",
        L"Có nội dung không mong đợi sau dòng chân trang."},
    TranslationPair{L"The number of data lines does not match the count in the header.",
        L"Số dòng dữ liệu không khớp với số đếm ở phần đầu."},
    TranslationPair{L"The file is not a valid QuickSift ratings export.",
        L"Tệp không phải bản xuất xếp hạng QuickSift hợp lệ."},
    TranslationPair{L"This file is not a valid QuickSift ratings export, so nothing was imported.",
        L"Tệp này không phải bản xuất xếp hạng QuickSift hợp lệ nên không có gì được nhập."},
    TranslationPair{L"Line {line}:",
        L"Dòng {line}:"},
    TranslationPair{L"Total files count mismatch. Expected is {expected} files, {present} files present. Continue?",
        L"Tổng số tệp không khớp. Dự kiến {expected} tệp, hiện có {present} tệp. Tiếp tục?"},
    TranslationPair{L"Continue",
        L"Tiếp tục"},
    TranslationPair{L"Some files to be modified are missing. Ignore and continue?",
        L"Một số tệp cần sửa đổi bị thiếu. Bỏ qua và tiếp tục?"},
    TranslationPair{L"{missing} of {listed} listed files are not in this folder.",
        L"{missing} trên {listed} tệp trong danh sách không có trong thư mục này."},
    TranslationPair{L"Yes",
        L"Có"},
    TranslationPair{L"See missing files list",
        L"Xem danh sách tệp bị thiếu"},
    TranslationPair{L"Import ratings for {count} files",
        L"Nhập xếp hạng cho {count} tệp"},
    TranslationPair{L"Are you sure? This cannot be undone.",
        L"Bạn có chắc không? Thao tác này không thể hoàn tác."},
    TranslationPair{L"Import",
        L"Nhập"},
    TranslationPair{L"Missing files",
        L"Tệp bị thiếu"},
    TranslationPair{L"Ratings import complete: {applied} applied, {skipped} skipped, {failed} failed.",
        L"Nhập xếp hạng hoàn tất: {applied} đã áp dụng, {skipped} bỏ qua, {failed} thất bại."},
    TranslationPair{L"Importing ratings…",
        L"Đang nhập xếp hạng…"},
    TranslationPair{L"External-change conflicts: {count}",
        L"Xung đột do thay đổi bên ngoài: {count}"},
    TranslationPair{L"Ratings import cancelled",
        L"Đã hủy nhập xếp hạng"},
    TranslationPair{L"Ratings import incomplete",
        L"Nhập xếp hạng chưa hoàn tất"},
    TranslationPair{L"Reading ratings file…",
        L"Đang đọc tệp xếp hạng…"},
    TranslationPair{L"Exporting ratings: {done} / {total} files",
        L"Đang xuất xếp hạng: {done} / {total} tệp"},
    TranslationPair{L"Importing ratings: {done} / {total} files",
        L"Đang nhập xếp hạng: {done} / {total} tệp"},
    TranslationPair{L"Ratings import",
        L"Nhập xếp hạng"},
    TranslationPair{L"Ratings import could not start",
        L"Không thể bắt đầu nhập xếp hạng"},
    TranslationPair{L"Face outlines: always shown (right-click to auto-hide)",
        L"Viền khuôn mặt: luôn hiển thị (nhấp chuột phải để tự ẩn)"},
    TranslationPair{L"Face outlines: auto-hide after 1 second (right-click to keep shown)",
        L"Viền khuôn mặt: tự ẩn sau 1 giây (nhấp chuột phải để luôn hiển thị)"},
    // 0.9.4.0 Batch Processing.
    TranslationPair{L"Batch Processing",
        L"Xử lý hàng loạt"},
    TranslationPair{L"File name list",
        L"Danh sách tên tệp"},
    TranslationPair{L"Type or paste file names, separated by spaces, commas, semicolons or new lines.",
        L"Nhập hoặc dán tên tệp, phân tách bằng dấu cách, dấu phẩy, dấu chấm phẩy hoặc xuống dòng."},
    TranslationPair{L"Load names from folder",
        L"Tải tên từ thư mục"},
    TranslationPair{L"Load names",
        L"Tải tên"},
    TranslationPair{L"Do what?",
        L"Làm gì?"},
    TranslationPair{L"Star rating",
        L"Số sao"},
    TranslationPair{L"Pick state",
        L"Trạng thái chọn"},
    TranslationPair{L"Color tag",
        L"Nhãn màu"},
    TranslationPair{L"No change",
        L"Không đổi"},
    TranslationPair{L"Additional settings",
        L"Cài đặt bổ sung"},
    TranslationPair{L"Extensions",
        L"Phần mở rộng"},
    TranslationPair{L"e.g. jpg, arw (empty = all supported types)",
        L"ví dụ: jpg, arw (để trống = mọi định dạng được hỗ trợ)"},
    TranslationPair{L"Matching method",
        L"Cách so khớp"},
    TranslationPair{L"Partial matching",
        L"So khớp một phần"},
    TranslationPair{L"Complete matching",
        L"So khớp toàn bộ"},
    TranslationPair{L"Execute",
        L"Thực hiện"},
    TranslationPair{L"Confirm",
        L"Xác nhận"},
    TranslationPair{L"Not a supported image type: {list}",
        L"Không phải định dạng ảnh được hỗ trợ: {list}"},
    TranslationPair{L"QuickSift could not open the Batch Processing window.",
        L"QuickSift không thể mở cửa sổ Xử lý hàng loạt."},
    TranslationPair{L"QuickSift could not start reading that folder.",
        L"QuickSift không thể bắt đầu đọc thư mục đó."},
    TranslationPair{L"Reading folder…",
        L"Đang đọc thư mục…"},
    TranslationPair{L"Enter at least one file name.",
        L"Hãy nhập ít nhất một tên tệp."},
    TranslationPair{L"Choose at least one change under \u201CDo what?\u201D.",
        L"Hãy chọn ít nhất một thay đổi trong mục \u201CLàm gì?\u201D."},
    TranslationPair{L"Fix the Extensions field: it lists a type QuickSift does not support.",
        L"Hãy sửa ô Phần mở rộng: ô này có định dạng QuickSift không hỗ trợ."},
    TranslationPair{L"QuickSift could not start Batch Processing.",
        L"QuickSift không thể bắt đầu Xử lý hàng loạt."},
    TranslationPair{L"Finding matching files…",
        L"Đang tìm các tệp khớp…"},
    TranslationPair{L"QuickSift could not read that folder.",
        L"QuickSift không thể đọc thư mục đó."},
    TranslationPair{L"That folder has no supported image files.",
        L"Thư mục đó không có tệp ảnh nào được hỗ trợ."},
    TranslationPair{L"The name list would be too long. Nothing was added.",
        L"Danh sách tên sẽ quá dài. Không có tên nào được thêm."},
    TranslationPair{L"Added {added} names ({duplicates} already listed)",
        L"Đã thêm {added} tên ({duplicates} tên đã có sẵn)"},
    TranslationPair{L"The folder changed. Check the settings and try again.",
        L"Thư mục đã thay đổi. Hãy kiểm tra cài đặt và thử lại."},
    TranslationPair{L"QuickSift could not prepare Batch Processing.",
        L"QuickSift không thể chuẩn bị Xử lý hàng loạt."},
    TranslationPair{L"Extensions: all supported image types (the Extensions field is empty).",
        L"Phần mở rộng: mọi định dạng ảnh được hỗ trợ (ô Phần mở rộng để trống)."},
    TranslationPair{L"No files in this folder match the names and extensions given. Nothing was changed.",
        L"Không có tệp nào trong thư mục này khớp với tên và phần mở rộng đã cho. Không có gì thay đổi."},
    TranslationPair{L"Batch operations: {count} files ({breakdown}) will be processed. Confirm? This cannot be undone.",
        L"Thao tác hàng loạt: {count} tệp ({breakdown}) sẽ được xử lý. Xác nhận? Thao tác này không thể hoàn tác."},
    TranslationPair{L"Batch operations: {count} file ({breakdown}) will be processed. Confirm? This cannot be undone.",
        L"Thao tác hàng loạt: {count} tệp ({breakdown}) sẽ được xử lý. Xác nhận? Thao tác này không thể hoàn tác."},
    TranslationPair{L"{count} name matched no file.",
        L"{count} tên không khớp với tệp nào."},
    TranslationPair{L"{count} names matched no file.",
        L"{count} tên không khớp với tệp nào."},
    TranslationPair{L"See unmatched names",
        L"Xem các tên không khớp"},
    TranslationPair{L"Unmatched names",
        L"Các tên không khớp"},
    TranslationPair{L"Batch processing",
        L"Xử lý hàng loạt"},
    TranslationPair{L"Batch processing…",
        L"Đang xử lý hàng loạt…"},
    TranslationPair{L"Batch processing: {done} / {total} files",
        L"Đang xử lý hàng loạt: {done} / {total} tệp"},
    TranslationPair{L"Batch processing: preparing…",
        L"Xử lý hàng loạt: đang chuẩn bị…"},
    TranslationPair{L"Batch processing complete: {applied} applied, {skipped} skipped, {failed} failed.",
        L"Xử lý hàng loạt hoàn tất: {applied} đã áp dụng, {skipped} bỏ qua, {failed} thất bại."},
    TranslationPair{L"Batch processing cancelled",
        L"Đã hủy xử lý hàng loạt"},
    TranslationPair{L"Batch processing incomplete",
        L"Xử lý hàng loạt chưa hoàn tất"},
    TranslationPair{L"Batch processing could not start",
        L"Không thể bắt đầu xử lý hàng loạt"},
    // Thumbnail search box
    TranslationPair{L"Search names, 8AM, date:12, 5 stars, red, picked\u2026",
        L"Tìm tên, 8AM, date:12, 5 sao, đỏ, đã chọn\u2026"},
    TranslationPair{L"{shown} of {total}", L"{shown} / {total}"},
    TranslationPair{L"Searching\u2026", L"Đang tìm\u2026"},
    TranslationPair{L"No photos match the search", L"Không có ảnh nào khớp với tìm kiếm"},
    // Status-bar filter tags (streamed into the status line, localized as fragments).
    TranslationPair{L"  •  date filter", L"  •  lọc ngày"},
    TranslationPair{L"  •  rating filter", L"  •  lọc xếp hạng"},
    TranslationPair{L"  •  pick filter", L"  •  lọc trạng thái chọn"},
    TranslationPair{L"  •  label filter", L"  •  lọc nhãn"},
    TranslationPair{L"  •  format filter", L"  •  lọc định dạng"},
    TranslationPair{L"  •  metadata pending", L"  •  metadata đang chờ"},
    // 0.9.4.0 polish: transaction/sidecar result details. They are often extended
    // with sizes or paths, so they live here as sentence fragments.
    TranslationPair{L"The metadata patch contains no supported change.",
        L"Bản vá metadata không chứa thay đổi nào được hỗ trợ."},
    TranslationPair{L"The XMP sidecar identity could not be captured safely.",
        L"Không thể ghi nhận an toàn danh tính của tệp XMP sidecar."},
    TranslationPair{L"The existing XMP sidecar could not be decoded, so QuickSift preserved it unchanged.",
        L"Không thể giải mã tệp XMP sidecar hiện có, vì vậy QuickSift giữ nguyên tệp đó."},
    TranslationPair{L"The rating property could not be updated without rewriting unfamiliar XMP.",
        L"Không thể cập nhật thuộc tính xếp hạng mà không ghi lại phần XMP lạ."},
    TranslationPair{L"The color-label property could not be updated without rewriting unfamiliar XMP.",
        L"Không thể cập nhật thuộc tính nhãn màu mà không ghi lại phần XMP lạ."},
    TranslationPair{L"The pick-state property could not be updated without rewriting unfamiliar XMP.",
        L"Không thể cập nhật thuộc tính trạng thái chọn mà không ghi lại phần XMP lạ."},
    TranslationPair{L"The XMP sidecar changed outside QuickSift, so the metadata update was stopped.",
        L"Tệp XMP sidecar đã bị thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"Another app is using the XMP sidecar, so it could not be published yet.",
        L"Một ứng dụng khác đang dùng tệp XMP sidecar nên chưa thể ghi tệp này."},
    TranslationPair{L"The XMP sidecar could not be published atomically.",
        L"Không thể ghi tệp XMP sidecar theo cách nguyên tử."},
    TranslationPair{L"The sidecar was written but the requested metadata could not be verified.",
        L"Tệp sidecar đã được ghi nhưng không thể xác minh metadata được yêu cầu."},
    TranslationPair{L"An unexpected XMP sidecar error occurred.",
        L"Đã xảy ra lỗi tệp XMP sidecar không mong muốn."},
    TranslationPair{L"The update is verified, but QuickSift retained the unchanged private backup at: ",
        L"Bản cập nhật đã được xác minh, nhưng QuickSift vẫn giữ bản sao lưu riêng chưa thay đổi tại: "},
    TranslationPair{L"This build does not include embedded-metadata support. Use XMP sidecars or build with build-release.cmd.",
        L"Bản dựng này không hỗ trợ metadata nhúng. Hãy dùng tệp XMP sidecar hoặc dựng lại bằng build-release.cmd."},
    TranslationPair{L"A guarded Undo primitive raised an exception.",
        L"Một thao tác Hoàn tác được bảo vệ đã phát sinh ngoại lệ."},
    TranslationPair{L"A guarded Redo primitive raised an exception.",
        L"Một thao tác Làm lại được bảo vệ đã phát sinh ngoại lệ."},
    TranslationPair{L"History media-group compensation was incomplete: ",
        L"Việc bù trừ nhóm tệp trong lịch sử chưa hoàn tất: "},
    TranslationPair{L"The operation was cancelled at a safe media-group boundary.",
        L"Thao tác đã bị hủy tại một ranh giới nhóm tệp an toàn."},
    TranslationPair{L"A verified file primitive raised an exception before completing its media group.",
        L"Một thao tác tệp có xác minh đã phát sinh ngoại lệ trước khi hoàn tất nhóm tệp của nó."},
    TranslationPair{L"A verified companion-file primitive raised an exception.",
        L"Một thao tác trên tệp đi kèm có xác minh đã phát sinh ngoại lệ."},
    TranslationPair{L"Media-group rollback was incomplete: ",
        L"Việc khôi phục nhóm tệp chưa hoàn tất: "},
    TranslationPair{L"The file transaction stopped after an unexpected exception at a safe boundary.",
        L"Giao dịch tệp đã dừng tại một ranh giới an toàn sau một ngoại lệ không mong muốn."},
    TranslationPair{L"The metadata batch was cancelled while waiting for a file to be released.",
        L"Lô cập nhật metadata đã bị hủy trong khi chờ một tệp được giải phóng."},
    TranslationPair{L"Skipped while waiting for the file to be released by another app.",
        L"Đã bỏ qua trong khi chờ ứng dụng khác giải phóng tệp."},
    TranslationPair{L"Active decoders could not be released for the metadata batch.",
        L"Không thể giải phóng các bộ giải mã đang hoạt động cho lô cập nhật metadata."},
    TranslationPair{L"The metadata batch was cancelled at a safe file boundary.",
        L"Lô cập nhật metadata đã bị hủy tại một ranh giới tệp an toàn."},
    TranslationPair{L" (Windows error ",
        L" (lỗi Windows "},
    TranslationPair{L"Source and destination identify the same path.",
        L"Nguồn và đích trỏ đến cùng một đường dẫn."},
    TranslationPair{L"The destination already exists or cannot be safely inspected.",
        L"Đích đã tồn tại hoặc không thể kiểm tra an toàn."},
    TranslationPair{L"The destination folder could not be created.",
        L"Không thể tạo thư mục đích."},
    TranslationPair{L"The source is not a normal, fingerprintable file.",
        L"Nguồn không phải là một tệp thông thường có thể lấy dấu vân tay."},
    TranslationPair{L"Memory allocation for the locked-handle rename failed.",
        L"Cấp phát bộ nhớ cho thao tác đổi tên bằng handle bị khóa đã thất bại."},
    TranslationPair{L"The source identity changed during the cross-volume handoff.",
        L"Danh tính của tệp nguồn đã thay đổi trong lúc chuyển giữa các ổ đĩa."},
    TranslationPair{L"The published destination changed before source retirement.",
        L"Tệp đích đã ghi bị thay đổi trước khi gỡ bỏ tệp nguồn."},
    TranslationPair{L"The source could not be retired after final destination verification.",
        L"Không thể gỡ bỏ tệp nguồn sau khi xác minh tệp đích lần cuối."},
    TranslationPair{L"The destination folder is unavailable or lacks verified-copy headroom.",
        L"Thư mục đích không khả dụng hoặc không đủ dung lượng dự phòng cho bản sao có xác minh."},
    TranslationPair{L"Could not reserve a private verification filename.",
        L"Không thể dành riêng một tên tệp xác minh riêng tư."},
    TranslationPair{L"Memory allocation for exact-object rollback failed.",
        L"Cấp phát bộ nhớ cho việc khôi phục chính xác đối tượng đã thất bại."},
    TranslationPair{L"Could not prepare handle-bound publication and rollback paths.",
        L"Không thể chuẩn bị các đường dẫn ghi và khôi phục gắn với handle."},
    TranslationPair{L"The folder could not be enumerated safely.",
        L"Không thể liệt kê thư mục một cách an toàn."},
    TranslationPair{L"The folder was only partially enumerated.",
        L"Thư mục chỉ được liệt kê một phần."},
    TranslationPair{L"\n\nWindows error: ",
        L"\n\nLỗi Windows: "},
    TranslationPair{L"The embedded metadata container could not be updated safely.",
        L"Không thể cập nhật an toàn vùng chứa metadata nhúng."},
    TranslationPair{L"\n\nRequired working space: ",
        L"\n\nDung lượng làm việc cần thiết: "},
    TranslationPair{L"\nAvailable space: ",
        L"\nDung lượng khả dụng: "},
    TranslationPair{L"A file expected at the operation destination is missing:\n\n",
        L"Thiếu một tệp lẽ ra phải có ở đích của thao tác:\n\n"},
    TranslationPair{L"A destination file changed after QuickSift created it:\n\n",
        L"Một tệp đích đã thay đổi sau khi QuickSift tạo ra nó:\n\n"},
    TranslationPair{L"The original location is no longer empty:\n\n",
        L"Vị trí ban đầu không còn trống:\n\n"},
    TranslationPair{L"There is not enough free space to restore files across volumes.\n\nRequired working space: ",
        L"Không đủ dung lượng trống để khôi phục tệp giữa các ổ đĩa.\n\nDung lượng làm việc cần thiết: "},
    TranslationPair{L"A source file is missing, inaccessible, symbolic, or not a normal file:\n\n",
        L"Một tệp nguồn bị thiếu, không truy cập được, là liên kết tượng trưng hoặc không phải tệp thông thường:\n\n"},
    TranslationPair{L"A source file changed after Undo, so Redo will not copy or move different bytes:\n\n",
        L"Một tệp nguồn đã thay đổi sau khi Hoàn tác, vì vậy Làm lại sẽ không sao chép hoặc di chuyển dữ liệu khác:\n\n"},
    TranslationPair{L"The source and destination resolve to the same path:\n\n",
        L"Nguồn và đích trỏ đến cùng một đường dẫn:\n\n"},
    TranslationPair{L"A destination filename is already occupied:\n\n",
        L"Một tên tệp đích đã bị chiếm:\n\n"},
    TranslationPair{L"There is not enough free space to repeat this file operation.\n\nRequired working space: ",
        L"Không đủ dung lượng trống để lặp lại thao tác tệp này.\n\nDung lượng làm việc cần thiết: "},
    TranslationPair{L"\n\nQuickSift left the entire operation untouched. It never overwrites or automatically renames files.",
        L"\n\nQuickSift giữ nguyên toàn bộ thao tác. Ứng dụng không bao giờ ghi đè hoặc tự động đổi tên tệp."},
    TranslationPair{L"The file's rating changed outside QuickSift, so the metadata update was stopped.",
        L"Xếp hạng của tệp đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The file's color label changed outside QuickSift, so the metadata update was stopped.",
        L"Nhãn màu của tệp đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The file's pick state changed outside QuickSift, so the metadata update was stopped.",
        L"Trạng thái chọn của tệp đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The file's metadata field changed outside QuickSift, so the metadata update was stopped.",
        L"Một trường metadata của tệp đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The image's rating changed outside QuickSift, so the metadata update was stopped.",
        L"Xếp hạng của ảnh đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The image's color label changed outside QuickSift, so the metadata update was stopped.",
        L"Nhãn màu của ảnh đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The image's pick state changed outside QuickSift, so the metadata update was stopped.",
        L"Trạng thái chọn của ảnh đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The image's metadata field changed outside QuickSift, so the metadata update was stopped.",
        L"Một trường metadata của ảnh đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata đã dừng."},
    TranslationPair{L"The JPEG's rating changed outside QuickSift, so the direct metadata update was stopped.",
        L"Xếp hạng của tệp JPEG đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata trực tiếp đã dừng."},
    TranslationPair{L"The JPEG's color label changed outside QuickSift, so the direct metadata update was stopped.",
        L"Nhãn màu của tệp JPEG đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata trực tiếp đã dừng."},
    TranslationPair{L"The JPEG's pick state changed outside QuickSift, so the direct metadata update was stopped.",
        L"Trạng thái chọn của tệp JPEG đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata trực tiếp đã dừng."},
    TranslationPair{L"The JPEG's metadata field changed outside QuickSift, so the direct metadata update was stopped.",
        L"Một trường metadata của tệp JPEG đã thay đổi bên ngoài QuickSift nên việc cập nhật metadata trực tiếp đã dừng."},
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

namespace {

// Built once (thread-safe function-local static). Exact keys come from both
// tables, first entry wins, so a whole-string match never goes through the
// fragment pass. Fragments are applied longest-first so that a short fragment
// ("Batch processing") can no longer pre-empt a longer, more specific one
// ("Batch processing cancelled") and leave half-English text behind. The
// portable checker (scripts/check-localization.py) mirrors this exact order.
struct LookupIndex {
    std::unordered_map<std::wstring_view, std::wstring_view> exact;
    std::vector<const TranslationPair*> fragments;
};

const LookupIndex& Index() {
    static const LookupIndex index = [] {
        LookupIndex built;
        built.exact.reserve(kTranslations.size() + kDynamicFragments.size());
        for (const auto& item : kTranslations) built.exact.emplace(item.english, item.vietnamese);
        for (const auto& item : kDynamicFragments) built.exact.emplace(item.english, item.vietnamese);
        built.fragments.reserve(kDynamicFragments.size());
        for (const auto& item : kDynamicFragments) built.fragments.push_back(&item);
        std::stable_sort(built.fragments.begin(), built.fragments.end(),
            [](const TranslationPair* a, const TranslationPair* b) {
                return a->english.size() > b->english.size();
            });
        return built;
    }();
    return index;
}

} // namespace

std::wstring Localizer::Text(std::wstring_view english) const {
    if (language_ == Language::English) return std::wstring(english);
    const LookupIndex& index = Index();
    if (const auto found = index.exact.find(english); found != index.exact.end())
        return std::wstring(found->second);
    std::wstring localized(english);
    for (const TranslationPair* item : index.fragments)
        ReplaceAll(localized, item->english, item->vietnamese);
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
• Tìm kiếm (chế độ Ảnh thu nhỏ): Ctrl+F (đặt con trỏ vào ô tìm kiếm), Enter/Tab/Mũi tên xuống (áp dụng và về lưới), Esc (xóa nội dung; bấm lần nữa để rời ô).

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
• Face Lock: khi có khuôn mặt, tự căn vùng nhìn vào khuôn mặt gần tâm ảnh nhất khi đổi ảnh hoặc thu phóng. Mỗi khuôn mặt được phát hiện có một khung mờ; nhấp vào khung để chọn khuôn mặt đó làm mục tiêu và phóng to để khuôn mặt chiếm khoảng 70% khung nhìn. Lựa chọn được ghi nhớ cho từng ảnh (bị xóa khi nội dung tệp thay đổi).
• Toàn màn hình / F11: lấp đầy màn hình và thu thanh tiêu đề vào mép trên. Giữ con trỏ ở vùng mép trên trong chốc lát để hiện thanh. Thanh tiêu đề nổi trên ảnh thay vì làm vùng xem ảnh đổi kích thước; đưa chuột ra xa để thanh tự ẩn lại.
• Viền đỏ quanh ảnh thu nhỏ: ít nhất một khuôn mặt có vẻ lệch nét, nhòe chuyển động hoặc đang nhắm mắt. Di chuột lên viền để xem khuôn mặt nào và lý do (ví dụ “Khuôn mặt 2: nhắm mắt”). Các ngưỡng được đặt thận trọng; khuôn mặt quá nhỏ không được đánh giá. Kính râm, mặt nghiêng hoặc nheo mắt có thể gây cảnh báo sai; nên kiểm tra ở 100% trước khi loại ảnh.
• Xoay trái / phải: xoay cách hiển thị; tệp gốc không bị thay đổi.

LỌC VÀ SẮP XẾP
• Stars, Pick state, Labels, Format và Date chỉ thay đổi những ảnh được hiển thị; không xóa tệp.
• Vùng chọn được giới hạn có chủ ý trong kết quả đang hiển thị. Khi bộ lọc ẩn một ảnh đã chọn, ảnh đó được bỏ khỏi vùng chọn để không trở thành mục tiêu lệnh vô hình.
• Bộ lọc metadata chỉ nhận giá trị đã đọc chắc chắn; ảnh đang chờ đọc, không đọc được hoặc có giá trị tùy chỉnh không bị giả vờ là chưa xếp hạng/chưa đánh dấu/chưa gắn nhãn.
• Sort thay đổi thứ tự hiển thị theo tên, ngày, xếp hạng hoặc định dạng.
• Thumbnail size thay đổi kích thước lưới.

TÌM KIẾM
• Ô tìm kiếm nổi ở góc trên bên phải, chỉ có trong chế độ Ảnh thu nhỏ. Bấm Ctrl+F hoặc nhấp vào ô. Kết quả cập nhật khi bạn gõ.
• Tìm theo tên tệp hoặc ghi chú người dùng (không phân biệt dấu: “dam cuoi” tìm được “đám cưới”), giờ chụp (8AM, 20h15), ngày (date:12, date:thứ sáu, 2026-10-12), số sao (5 sao, >=4 stars, unrated), nhãn màu (đỏ, xanh lá) và trạng thái chọn (đã chọn, loại).
• Đặt chữ trong dấu ngoặc kép (“red”) hoặc dùng name:/comment: để tìm đúng chữ đó trong tên hoặc ghi chú.
• Tìm kiếm kết hợp với các bộ lọc (mọi điều kiện đều phải khớp) và vẫn được giữ khi mở một kết quả ở chế độ Một ảnh. Bộ đếm hiển thị số ảnh hiện ra trên tổng số.

LƯU XẾP HẠNG, CHỌN/LOẠI VÀ NHÃN
• Mỗi thay đổi được lưu ở chế độ nền; giao diện không bao giờ phải chờ việc ghi tệp. Khi bạn đổi nhanh nhiều lần, chỉ giá trị cuối cùng được ghi, theo đúng thứ tự cho từng tệp.
• Nếu một ứng dụng khác (trình chỉnh ảnh, công cụ đồng bộ, trình quét virus) đang dùng tệp, QuickSift tự thử lại trong vài giây. Nếu tệp vẫn bị khóa, thông báo “Đang chờ <tệp> — tệp đang được ứng dụng khác sử dụng” xuất hiện và QuickSift tiếp tục chờ tối đa 30 giây; sau đó bạn có thể chọn Thử lại hoặc Bỏ qua. Khi bỏ qua hoặc thất bại, giá trị trước đó được hiển thị lại.
• QuickSift chỉ dừng hẳn khi việc ghi có thể làm hỏng tệp (tệp đã thay đổi bên ngoài, thiếu dung lượng, phương tiện chỉ đọc); khi đó không có gì bị ghi đè.

NHẬP/XUẤT XẾP HẠNG VÀ XỬ LÝ HÀNG LOẠT
• Xuất xếp hạng lưu số sao, trạng thái chọn và nhãn màu của các tệp trong thư mục vào một tệp văn bản UTF-8; Nhập xếp hạng áp dụng lại cho các tệp cùng tên. Định dạng tệp xuất (dòng đầu, dòng cuối và tên cột) luôn bằng tiếng Anh để có thể dùng chung giữa các ngôn ngữ.
• Xử lý hàng loạt áp dụng xếp hạng, trạng thái chọn hoặc nhãn cho danh sách tên tệp được dán hoặc nạp từ một thư mục khác.

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
• Cảnh báo khuôn mặt đánh giá từng khuôn mặt trên ảnh giải mã độ phân giải cao: độ chi tiết Laplacian chuẩn hóa theo kích thước và độ tương phản (lệch nét), độ dị hướng của chi tiết (nhòe chuyển động), và một phép kiểm tra lòng trắng mắt thận trọng (nhắm mắt). Tất cả chạy trên luồng nền và được lưu vào bộ nhớ đệm.
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
• Face Lock: when a face is available, recenters on the face closest to the image centre when changing photos or zooming. Every detected face gets a faint outline; click an outline to make that face the target and zoom so it fills about 70% of the pane. The choice is remembered per photo (cleared if the file content changes).
• Fullscreen / F11 fills the monitor and retracts the title bar into the top edge. Rest the pointer briefly against the top edge to reveal it. The bar floats over the photograph without resizing the image view; move away and it hides again.
• Red thumbnail border: at least one face looks out of focus, motion-blurred, or has its eyes closed. Hover the border to see which face and why (for example “Face 2: eyes closed”). Thresholds are conservative and very small faces are not judged. Sunglasses, profiles, or squinting can cause false warnings; verify at 100% before rejecting the photo.
• Rotate left / right changes only the display orientation; it does not rotate the original file.

FILTERING AND SORTING
• Stars, Pick state, Labels, Format, and Date change what is visible; they do not delete files.
• Selection is intentionally limited to visible results. If a filter hides a selected photo, it is removed from selection so it cannot remain an invisible command target.
• Metadata filters accept only authoritative values; pending, unreadable, and unsupported custom values do not masquerade as unrated, unmarked, or unlabeled.
• Sort changes display order by name, date, rating, or format.
• Thumbnail size changes the grid density.

SEARCH
• Thumbnail view has a floating search field at the top right. Press Ctrl+F or click it; results update as you type. Enter, Tab or Down apply the query and return to the grid; Esc clears it, and a second Esc leaves the field.
• Search matches file names and user comments (accent-insensitive: “dam cuoi” finds “đám cưới”), capture times (8AM, 20h15), dates (date:12, date:friday, 2026-10-12), stars (5 stars, >=4 stars, unrated), colour labels (red, green) and pick states (picked, rejected).
• Quote a word (“red”) or use name:/comment: to search for that literal text.
• Search is combined with the Filter tab (every term must match) and stays applied when you open a result in Single view. The counter shows shown of total.

SAVING RATINGS, PICKS AND LABELS
• Every change is saved in the background; the interface never waits for a file write. Rapid repeated changes are coalesced: the last value wins and per-file order is kept.
• If another app (an editor, a sync client, a virus scanner) is using a file, QuickSift retries for a few seconds. If the file stays locked, a “Waiting for <file> — in use by another app” notice appears and QuickSift keeps waiting for up to 30 seconds; then you can choose Retry or Skip. A skipped or failed change shows the previous value again.
• QuickSift stops for good only when writing could corrupt a file (it changed outside QuickSift, storage is low, or the media is read-only); nothing is overwritten.

IMPORT/EXPORT RATINGS AND BATCH PROCESSING
• Export Ratings saves the stars, pick state and colour label of the folder's files to a UTF-8 text file; Import Ratings applies them again to files with the same names. The export file format (header, footer and column names) is always English so files can be shared between languages.
• Batch Processing applies a rating, pick state or label to a list of file names that you paste or load from another folder.

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
• The face warning analyzes every face on a high-resolution decode: Laplacian detail normalized by face size and contrast (defocus), directional detail anisotropy (motion blur), and a conservative sclera-visibility check (closed eyes). All analysis runs on worker threads and is cached.
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
