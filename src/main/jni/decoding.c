#include "gif.h"

void DDGifSlurp(GifInfo *info, bool shouldDecode) {
    GifRecordType RecordType;
    GifByteType *ExtData;
    int ExtFunction;
    GifFileType *gifFilePtr;
    gifFilePtr = info->gifFilePtr;
    do {
        if (DGifGetRecordType(gifFilePtr, &RecordType) == GIF_ERROR)
            return;
        switch (RecordType) {
            case IMAGE_DESC_RECORD_TYPE:

                if (DGifGetImageDesc(gifFilePtr, !shouldDecode) == GIF_ERROR)
                    return;

                if (!shouldDecode) {
                    SavedImage *sp = &gifFilePtr->SavedImages[gifFilePtr->ImageCount - 1];

                    int_fast32_t topOverflow = gifFilePtr->Image.Top + gifFilePtr->Image.Height - gifFilePtr->SHeight;
                    if (topOverflow > 0) {
                        sp->ImageDesc.Top -= topOverflow;
                    }

                    int_fast32_t leftOverflow = gifFilePtr->Image.Left + gifFilePtr->Image.Width - gifFilePtr->SWidth;
                    if (leftOverflow > 0) {
                        sp->ImageDesc.Left -= leftOverflow;
                    }
                }

                uint_fast16_t widthOverflow = gifFilePtr->Image.Width - gifFilePtr->SWidth;
                uint_fast16_t heightOverflow = gifFilePtr->Image.Height - gifFilePtr->SHeight;
                if (widthOverflow > 0 || heightOverflow > 0) {
                    gifFilePtr->SWidth += widthOverflow;
                    gifFilePtr->SHeight += heightOverflow;

                    if (shouldDecode) {
                        void *tmpRasterBits = reallocarray(info->rasterBits, gifFilePtr->SWidth * gifFilePtr->SHeight, sizeof(GifPixelType));
                        if (tmpRasterBits == NULL) {
                            gifFilePtr->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
                            return;
                        }
                        info->rasterBits = tmpRasterBits;
                    }
                }

                if (shouldDecode) {
                    if (gifFilePtr->Image.Interlace) {
                        uint_fast16_t i, j;
                        /*
                         * The way an interlaced image should be read -
                         * offsets and jumps...
                         */
                        uint_fast8_t InterlacedOffset[] = {0, 4, 2, 1};
                        uint_fast8_t InterlacedJumps[] = {8, 8, 4, 2};
                        /* Need to perform 4 passes on the image */
                        for (i = 0; i < 4; i++)
                            for (j = InterlacedOffset[i]; j < gifFilePtr->Image.Height; j += InterlacedJumps[i]) {
                                if (DGifGetLine(gifFilePtr, info->rasterBits + j * gifFilePtr->Image.Width, gifFilePtr->Image.Width) == GIF_ERROR)
                                    return;
                            }
                    }
                    else {
                        if (DGifGetLine(gifFilePtr, info->rasterBits,gifFilePtr->Image.Width * gifFilePtr->Image.Height) == GIF_ERROR)
                            return;
                    }
                    return;
                }
                else {
                    do
                        if (DGifGetCodeNext(gifFilePtr, &ExtData) == GIF_ERROR)
                            return;
                    while (ExtData != NULL);
                }
                break;

            case EXTENSION_RECORD_TYPE:
                if (DGifGetExtension(gifFilePtr, &ExtFunction, &ExtData) == GIF_ERROR)
                    return;
                if (!shouldDecode) {
                    GraphicsControlBlock *tmpInfos = reallocarray(info->controlBlock, info->gifFilePtr->ImageCount + 1 , sizeof(GraphicsControlBlock));
                    if (tmpInfos == NULL) {
                        gifFilePtr->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
                        return;
                    }
                    info->controlBlock = tmpInfos;
                    info->controlBlock[gifFilePtr->ImageCount].DelayTime = DEFAULT_FRAME_DURATION_MS;
                    if (readExtensions(ExtFunction, ExtData, info) == GIF_ERROR)
                        return;
                }
                while (ExtData != NULL) {
                    if (DGifGetExtensionNext(info->gifFilePtr, &ExtData) == GIF_ERROR)
                        return;
                    if (!shouldDecode) {
                        if (readExtensions(ExtFunction, ExtData, info) == GIF_ERROR)
                            return;
                    }
                }
                break;

            case TERMINATE_RECORD_TYPE:
                break;

            default: /* Should be trapped by DGifGetRecordType */
                break;
        }
    } while (RecordType != TERMINATE_RECORD_TYPE);

    info->rewindFunction(info);
}

static int readExtensions(int ExtFunction, GifByteType *ExtData, GifInfo *info) {
    if (ExtData == NULL)
        return GIF_OK;
    if (ExtFunction == GRAPHICS_EXT_FUNC_CODE) {
        GraphicsControlBlock *GCB = &info->controlBlock[info->gifFilePtr->ImageCount];
        if (DGifExtensionToGCB(ExtData[0], ExtData + 1, GCB) == GIF_ERROR)
            return GIF_ERROR;

        GCB->DelayTime = GCB->DelayTime > 1 ? GCB->DelayTime * 10 : DEFAULT_FRAME_DURATION_MS;
    }
    else if (ExtFunction == COMMENT_EXT_FUNC_CODE) {
        if (getComment(ExtData, info) == GIF_ERROR) {
            info->gifFilePtr->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
            return GIF_ERROR;
        }
    }
    else if (ExtFunction == APPLICATION_EXT_FUNC_CODE) {
        char const *string = (char const *) (ExtData + 1);
        if (strncmp("NETSCAPE2.0", string, ExtData[0]) == 0
            || strncmp("ANIMEXTS1.0", string, ExtData[0]) == 0) {
            if (DGifGetExtensionNext(info->gifFilePtr, &ExtData) == GIF_ERROR)
                return GIF_ERROR;
            if (ExtData[0] == 3 && ExtData[1] == 1) {
                info->loopCount = (uint_fast16_t) (ExtData[2] + (ExtData[3] << 8));
            }
        }
    }
    return GIF_OK;
}

static int getComment(GifByteType *Bytes, GifInfo *info) {
    unsigned int len = (unsigned int) Bytes[0];
    size_t offset = info->comment != NULL ? strlen(info->comment) : 0;
    char *ret = reallocarray(info->comment, len + offset + 1, sizeof(char));
    if (ret != NULL) {
        memcpy(ret + offset, &Bytes[1], len);
        ret[len + offset] = 0;
        info->comment = ret;
        return GIF_OK;
    }
    info->gifFilePtr->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
    return GIF_ERROR;
}
