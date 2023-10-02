#include "mime_type/mime_type.h"
#include <filesystem>

namespace ec_prv {
namespace mime_type {

auto string(MimeType src) -> std::string_view {
  switch (src) {
  case MimeType::HTML:
    return "text/html";
  case MimeType::JS:
    return "text/javascript";
  case MimeType::CSS:
    return "text/css";
  case MimeType::JPEG:
    return "image/jpeg";
  case MimeType::PNG:
    return "image/png";
  case MimeType::ICO:
    return "image/vnd.microsoft.icon";
  case MimeType::TEXT:
    return "text/plain";
  case MimeType::WOFF2:
    return "font/woff2";
  case MimeType::WOFF:
    return "font/woff";
  case MimeType::WEBP:
    return "image/webp";
  case MimeType::JSON:
    return "application/json";
  case MimeType::JSON_LD:
    return "application/ld+json";
  case MimeType::SVG:
    return "image/svg+xml";
  case MimeType::PDF:
    return "application/pdf";
  case MimeType::OGG_AUDIO:
    return "audio/ogg";
  case MimeType::OGG_VIDEO:
    return "video/ogg";
  case MimeType::OGG:
    return "application/ogg";
  case MimeType::OPENTYPE_FONT:
    return "font/otf";
  case MimeType::MP3:
    return "audio/mpeg";
  case MimeType::MP4:
    return "video/mp4";
  case MimeType::MPEG:
    return "video/mpeg";
  case MimeType::OCTET_STREAM:
  default:
    return "application/octet-stream";
  }
}

auto infer_mime_type(std::string_view filename) -> MimeType {
  std::filesystem::path p{filename};
  auto ext = p.extension();
  if (ext == ".html" || ext == ".htm") {
    return MimeType::HTML;
  } else if (ext == ".js") {
    return MimeType::JS;
  } else if (ext == ".css") {
    return MimeType::CSS;
  } else if (ext == ".woff2") {
    return MimeType::WOFF2;
  } else if (ext == ".woff") {
    return MimeType::WOFF;
  } else if (ext == ".jpeg" || ext == ".jpg") {
    return MimeType::JPEG;
  } else if (ext == ".png") {
    return MimeType::PNG;
  } else if (ext == ".svg") {
    return MimeType::SVG;
  } else if (ext == ".ico") {
    return MimeType::ICO;
  } else if (ext == ".txt" || ext == ".md" || ext == ".rst") {
    return MimeType::TEXT;
  } else if (ext == ".json") {
    return MimeType::JSON;
  } else if (ext == ".jsonld") {
    return MimeType::JSON_LD;
  } else if (ext == ".otf") {
    return MimeType::OPENTYPE_FONT;
  } else if (ext == ".ogg") {
    return MimeType::OGG;
  } else if (ext == ".ogv") {
    return MimeType::OGG_VIDEO;
  } else if (ext == "oga") {
    return MimeType::OGG_AUDIO;
  } else if (ext == ".png") {
    return MimeType::PDF;
  } else if (ext == ".mp3") {
    return MimeType::MP3;
  } else if (ext == ".mpeg") {
    return MimeType::MPEG;
  } else if (ext == ".mp4") {
    return MimeType::MP4;
  }
  return MimeType::OCTET_STREAM;
}

} // namespace mime_type
} // namespace ec_prv
