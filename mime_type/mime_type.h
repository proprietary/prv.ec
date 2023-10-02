#ifndef _INCLUDE_EC_PRV_MIME_TYPE_MIME_TYPE_H
#define _INCLUDE_EC_PRV_MIME_TYPE_MIME_TYPE_H

#include <string>
#include <string_view>

namespace ec_prv {
namespace mime_type {

enum struct MimeType {
  OCTET_STREAM,
  HTML,
  CSS,
  JS,
  JPEG,
  PNG,
  WEBP,
  TEXT,
  WOFF2,
  WOFF,
  ICO,
  JSON,
  JSON_LD,
  SVG,
  OGG_AUDIO,
  OGG_VIDEO,
  OGG,
  OPUS,
  OPENTYPE_FONT,
  PDF,
  MPEG,
  MP4,
  MP3,
};

auto string(MimeType src) -> std::string_view;

auto infer_mime_type(std::string_view filename) -> MimeType;

} // namespace mime_type
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_MIME_TYPE_MIME_TYPE_H
