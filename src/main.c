#include <efi.h>
#include <efilib.h>

#include <elf.h>

#include "zenithBoot.h"

#define ALIGN_TO(mem, align) ((mem + align - 1) & ~(align-1))

/* minimal bootloader written by solidracer */

typedef VOID (*ENTRY)(VOID);

static VOID waitKeyPress(VOID) {
    Print(L"Press any key to continue...");
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, NULL);
    gST->ConIn->ReadKeyStroke(gST->ConIn, NULL);
    Print(L"\r\n");
}

static VOID reportWarn(CHAR16 *str) {
    Print(L"[WARNING] %s\r\n", str);
}

static VOID reportError(CHAR16 *str, EFI_STATUS ERR) {
    Print(L"[ERROR] %s: %r\r\n", str, ERR);
}

static VOID reportErrorRaw(CHAR16 *str, CHAR16 *ERR) {
    Print(L"[ERROR] %s: %s\r\n", str, ERR);
}

static EFI_STATUS getFS(EFI_HANDLE handle, EFI_SIMPLE_FILE_SYSTEM_PROTOCOL **FS) {
    EFI_STATUS stat;
    EFI_LOADED_IMAGE_PROTOCOL *img;

    stat = gBS->HandleProtocol(
        handle,
        &gEfiLoadedImageProtocolGuid,
        (VOID**)&img
    );
    if (EFI_ERROR(stat)) return stat;

    stat = gBS->HandleProtocol(
        img->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)FS
    );
    if (EFI_ERROR(stat)) return stat;

    return stat;
}

static EFI_STATUS getGOP(EFI_GRAPHICS_OUTPUT_PROTOCOL **gop) {
    EFI_STATUS stat;

    stat = gBS->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID**)gop
    );
    if (EFI_ERROR(stat)) return stat;

    return stat;
}

static BOOLEAN isELF(Elf64_Ehdr *hdr) {
    if (
        hdr                              &&
        hdr->e_ident[EI_MAG0] == ELFMAG0 &&
        hdr->e_ident[EI_MAG1] == ELFMAG1 &&
        hdr->e_ident[EI_MAG2] == ELFMAG2 &&
        hdr->e_ident[EI_MAG3] == ELFMAG3
    ) return TRUE;
    return FALSE;
}

static BOOLEAN isELFSupported(Elf64_Ehdr *hdr) {
    if (
        hdr->e_ident[EI_CLASS]  == ELFCLASS64   &&
        hdr->e_ident[EI_DATA]   == ELFDATA2LSB  &&
        hdr->e_machine          == EM_X86_64    &&
        hdr->e_ident[EI_VERSION]== EV_CURRENT   &&
        hdr->e_type             == ET_EXEC
    ) return TRUE;
    return FALSE;
}

EFI_STATUS efi_main(EFI_HANDLE imghandle, EFI_SYSTEM_TABLE *systab) {
    InitializeLib(imghandle, systab);

    EFI_STATUS stat;

    UINTN rows, columns;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *kernel;

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    UINT16 warn = 0;

    gST->ConOut->QueryMode(
        gST->ConOut,
        gST->ConOut->Mode->Mode,
        &columns,
        &rows
    );

    /* some init */
    gST->ConOut->ClearScreen(gST->ConOut);
    gST->ConOut->EnableCursor(gST->ConOut, TRUE);

    /* im pretty sure theres a better way to do this (the color) */
    gST->ConOut->SetAttribute(gST->ConOut, EFI_CYAN);
    PrintAt((columns - sizeof("zenithBoot (x86_64 EFI)")) / 2, 0, L"zenithBoot");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_DARKGRAY);
    Print(L" (x86_64 EFI)\r\n");
    gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGRAY);

    for (UINT32 c = 0;c<columns;c++)
        PrintAt(c, 1, L"%c", 0x2550);

    Print(L"\r\n");

    stat = getGOP(&gop);
    if (EFI_ERROR(stat)) {
        reportError(L"getGOP()", stat);
        waitKeyPress();
        return stat;
    }

    stat = getFS(imghandle, &fs);
    if (EFI_ERROR(stat)) {
        reportError(L"getFS()", stat);
        waitKeyPress();
        return stat;
    }

    stat = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(stat)) {
        reportError(L"fs->OpenVolume()", stat);
        waitKeyPress();
        return stat;
    }

    stat = root->Open(
        root,
        &kernel,
        L"\\kernel.elf",
        EFI_FILE_MODE_READ,
        EFI_FILE_READ_ONLY
    );
    if (EFI_ERROR(stat)) {
        reportError(L"root->Open(\"\\kernel.elf\")", stat);
        waitKeyPress();
        return stat;
    }

    Elf64_Ehdr elf_kernel;
    UINTN size = sizeof(Elf64_Ehdr);
    stat = kernel->Read(
        kernel,
        &size,
        &elf_kernel
    );
    if (EFI_ERROR(stat)) {
        reportError(L"kernel->Read()", stat);
        waitKeyPress();
        return stat;
    }
    else if (size != sizeof(Elf64_Ehdr)) {
        reportWarn(L"Could not read the entire ELF header, This may lead to corruption of the staged kernel.");
        warn++;
    }
    
    if (!isELF(&elf_kernel)) {
        reportErrorRaw(L"isELF(kernel)", L"kernel is not elf");
        waitKeyPress();
        return 1;
    }
    else if (!isELFSupported(&elf_kernel)) {
        reportErrorRaw(L"isELFSupported(kernel)", L"kernel is not a supported elf type");
        waitKeyPress();
        return 1;
    }

    INT32 found_mode = -1;
    for (UINT32 i = 0;i<elf_kernel.e_phnum;i++) {
        Elf64_Phdr phdr;
        UINTN size = elf_kernel.e_phentsize;
        EFI_MEMORY_TYPE memtype = EfiLoaderData;
        kernel->SetPosition(kernel, elf_kernel.e_phoff + elf_kernel.e_phentsize * i);
        kernel->Read(kernel, &size, &phdr);
        /* start parsing the header */
        if (phdr.p_type == PT_LOAD) {
            if (phdr.p_flags & PF_X) {
                if (phdr.p_flags & PF_W) {
                    reportWarn(L"Segment is against W^X policy");
                    warn++;
                }
                memtype = EfiLoaderCode;
            }
            size = phdr.p_filesz;
            CHAR8 *memory = (CHAR8*)phdr.p_paddr;
            UINTN pagesiz = EFI_SIZE_TO_PAGES(phdr.p_memsz);
            gBS->AllocatePages(AllocateAddress, memtype, pagesiz, (VOID*)memory);
            SetMem(memory, phdr.p_memsz, 0);
            kernel->SetPosition(kernel, phdr.p_offset);
            if (size) {
                kernel->Read(kernel, &size, memory);
                if (size != phdr.p_filesz) {
                    reportWarn(L"Could not read the whole segment, zeroing unread memory");
                    warn++;
                }
            }
            if (!i) {
                if (CompareMem(memory, (CHAR8[]){ 'Z', 'E', 'N', '1' }, 4)) {
                    reportErrorRaw(L"validateHeader", L"boot magic number is wrong");
                    waitKeyPress();
                    return 1;
                }
                zenith_boot_info_t *info = (zenith_boot_info_t*)ALIGN_TO((UINTN)(memory + 4), 8);
                /* set the resolution of GOP */
                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *gop_info;
                for (INT32 m = 0;m<gop->Mode->MaxMode;m++) {
                    UINTN info_size = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
                    stat = gop->QueryMode(
                        gop,
                        m,
                        &info_size,
                        &gop_info
                    );
                    if (EFI_ERROR(stat)) {
                        reportError(L"gop->QueryMode()", stat);
                        waitKeyPress();
                        return stat;
                    }
                    else if (gop_info->HorizontalResolution == 640 && gop_info->VerticalResolution == 480
                        && (zenith_fb_pixel_format_t) gop_info->PixelFormat <= PF_BGRR_32BBP) {
                        found_mode = m;
                        break;
                    }
                }
                if (found_mode==-1) {
                    reportErrorRaw(L"readyGOP()", L"640x480 not supported");
                    waitKeyPress();
                    return 1;
                }
                info->width = gop_info->HorizontalResolution;
                info->height = gop_info->VerticalResolution;
                info->ppsl = gop_info->PixelsPerScanLine;
                info->fb = (VOID*)gop->Mode->FrameBufferBase;
                info->pixel_format = (zenith_fb_pixel_format_t)gop_info->PixelFormat;
            }
            Print(L"Kernel segment loaded at 0x%X with size 0x%X (0x%X zeroed)\r\n", phdr.p_paddr, phdr.p_filesz, phdr.p_memsz - phdr.p_filesz);
        }
    }

    if (warn) {
        Print(L"%d WARNINGS REPORTED\r\n", warn);
        waitKeyPress();
    }

    Print(L"CLEANING UP\r\n");
    kernel->Close(kernel);
    root->Close(root);

    #ifdef ZENITH_DEBUG
        waitKeyPress();
    #endif

    gop->SetMode(
        gop,
        found_mode
    );

    UINTN mapsz = 0, key, descsz;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINT32 descrver;

    /* lastly, exit boot services and call kernel entry */

    stat = gBS->GetMemoryMap(&mapsz, map, NULL, &descsz, NULL);
    if (stat != EFI_BUFFER_TOO_SMALL)
        return 1;

    mapsz += 2 * descsz;

    stat = gBS->AllocatePool(EfiLoaderData, mapsz, (VOID**)&map);
    if (EFI_ERROR(stat))
        return stat;

    stat = gBS->GetMemoryMap(&mapsz, map, &key, &descsz, &descrver);
    if (EFI_ERROR(stat))
        return stat;

    stat = gBS->ExitBootServices(imghandle, key);
    if (EFI_ERROR(stat))
        return stat;

    ENTRY entry = (ENTRY)elf_kernel.e_entry;
    entry();

    __builtin_unreachable();
}
