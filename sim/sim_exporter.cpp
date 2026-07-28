// ---------------------------------------------------------------------------
//  sim_exporter.cpp - microSD export is hardware only
// ---------------------------------------------------------------------------
//
//  The Save panel is still drawn and still reachable in the preview so the
//  documentation can show it, but every write reports plainly that there is no
//  card in a browser rather than pretending to succeed.
//
#include "export/exporter.h"

#include <stdio.h>
#include <string.h>

namespace {
void notAvailable(char* err, size_t n) {
    snprintf(err, n, "no microSD in the browser preview");
}
}  // namespace

bool Exporter::mount() {
    notAvailable(_err, sizeof(_err));
    _mounted = false;
    return false;
}

void Exporter::unmount() { _mounted = false; }

bool Exporter::ensureDir() { return false; }

bool Exporter::nextPath(const char*, const char*) { return false; }

bool Exporter::writeCsv(const CaptureBuffer&, const CaptureInfo&,
                        const ChannelConfig[LA_MAX_CHANNELS], uint32_t, uint32_t) {
    notAvailable(_err, sizeof(_err));
    return false;
}

bool Exporter::writeVcd(const CaptureBuffer&, const CaptureInfo&,
                        const ChannelConfig[LA_MAX_CHANNELS]) {
    notAvailable(_err, sizeof(_err));
    return false;
}

bool Exporter::writeAnnotations(const AnnotationList&, const CaptureInfo&, const char*) {
    notAvailable(_err, sizeof(_err));
    return false;
}

bool Exporter::writeScreenshot() {
    notAvailable(_err, sizeof(_err));
    return false;
}
