#include "form_data.hpp"

namespace ampify {

std::string MultipartBody(const std::vector<FormField>& fields, std::string_view boundary) {
    std::string body;
    for (const FormField& field : fields) {
        body += "--";
        body += boundary;
        body += "\r\nContent-Disposition: form-data; name=\"" + field.name + "\"";
        if (!field.filename.empty()) {
            body += "; filename=\"" + field.filename + "\"\r\nContent-Type: ";
            body += field.contentType.empty() ? "application/octet-stream" : field.contentType;
        }
        body += "\r\n\r\n";
        body += field.value;
        body += "\r\n";
    }
    body += "--";
    body += boundary;
    body += "--\r\n";
    return body;
}

}  // namespace ampify
