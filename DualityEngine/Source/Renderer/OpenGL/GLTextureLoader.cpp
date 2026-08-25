#include "DualityEngine/Renderer/OpenGL/GLTextureLoader.h"

#include <GL/glew.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Duality {

    unsigned int GLTextureLoader::LoadTextureFromFile(const std::string& path, const TextureImportSettings& settings) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data)
            return 0;

        GLint magMinFilter = settings.FilterMode == TextureFilterMode::Point ? GL_NEAREST : GL_LINEAR;
        GLint minFilter = magMinFilter;
        if (settings.GenerateMipmaps) {
            minFilter = settings.FilterMode == TextureFilterMode::Point
                ? GL_NEAREST_MIPMAP_NEAREST
                : GL_LINEAR_MIPMAP_LINEAR;
        }
        GLint wrap = settings.WrapMode == TextureWrapMode::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magMinFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        if (settings.GenerateMipmaps)
            glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(data);
        return texture;
    }

}
