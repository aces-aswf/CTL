#include "ImageIO.h"

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImathBox.h>
#include <IexBaseExc.h>

#include <cstring>
#include <sstream>

namespace ctltest {

Image readImage(const std::string& path) {
    try {
        Imf::InputFile file(path.c_str());
        const Imath::Box2i dw = file.header().dataWindow();
        const int width  = dw.max.x - dw.min.x + 1;
        const int height = dw.max.y - dw.min.y + 1;
        if (width <= 0 || height <= 0) {
            throw ImageIOError(path + ": non-positive data window");
        }

        Image img;
        img.width  = width;
        img.height = height;

        const Imf::ChannelList& cl = file.header().channels();
        Imf::FrameBuffer fb;

        for (Imf::ChannelList::ConstIterator it = cl.begin(); it != cl.end(); ++it) {
            const std::string chName = it.name();
            img.channels[chName].assign(static_cast<size_t>(width) * height, 0.0f);
            float* base = img.channels[chName].data();
            const ptrdiff_t xstride = sizeof(float);
            const ptrdiff_t ystride = sizeof(float) * static_cast<ptrdiff_t>(width);
            char* origin = reinterpret_cast<char*>(base)
                         - dw.min.x * xstride
                         - dw.min.y * ystride;
            fb.insert(chName, Imf::Slice(Imf::FLOAT, origin, xstride, ystride, 1, 1, 0.0));
        }

        file.setFrameBuffer(fb);
        file.readPixels(dw.min.y, dw.max.y);
        return img;
    } catch (const Iex::BaseExc& e) {
        throw ImageIOError(path + ": " + e.what());
    } catch (const std::exception& e) {
        throw ImageIOError(path + ": " + e.what());
    }
}

void writeImage(const std::string& path, const Image& img) {
    if (img.empty()) throw ImageIOError(path + ": refusing to write empty image");

    try {
        Imf::Header header(img.width, img.height);
        for (const auto& kv : img.channels) {
            header.channels().insert(kv.first, Imf::Channel(Imf::FLOAT));
        }

        Imf::FrameBuffer fb;
        for (const auto& kv : img.channels) {
            if (kv.second.size() != static_cast<size_t>(img.width) * img.height) {
                std::ostringstream os;
                os << path << ": channel '" << kv.first << "' has "
                   << kv.second.size() << " pixels; expected "
                   << (static_cast<size_t>(img.width) * img.height);
                throw ImageIOError(os.str());
            }
            const float* base = kv.second.data();
            const ptrdiff_t xstride = sizeof(float);
            const ptrdiff_t ystride = sizeof(float) * static_cast<ptrdiff_t>(img.width);
            fb.insert(kv.first, Imf::Slice(Imf::FLOAT,
                                           reinterpret_cast<char*>(const_cast<float*>(base)),
                                           xstride, ystride));
        }

        Imf::OutputFile file(path.c_str(), header);
        file.setFrameBuffer(fb);
        file.writePixels(img.height);
    } catch (const Iex::BaseExc& e) {
        throw ImageIOError(path + ": " + e.what());
    } catch (const std::exception& e) {
        throw ImageIOError(path + ": " + e.what());
    }
}

} // namespace ctltest
