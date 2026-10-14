package me.magnum.melonds.impl.emulator

import android.net.Uri
import me.magnum.melonds.common.uridelegates.UriHandler
import me.magnum.melonds.domain.model.rom.Rom
import me.magnum.melonds.domain.repositories.SettingsRepository

class SramProvider(
    private val settingsRepository: SettingsRepository,
    private val uriHandler: UriHandler,
) {

    /**
     * @param nameSuffix Added before the extension, to give a second copy of the same game its own save file
     */
    @Throws(SramLoadException::class)
    fun getSramForRom(rom: Rom, nameSuffix: String = ""): Uri {
        val rootDirUri = settingsRepository.getSaveFileDirectory(rom)

        val rootDocument = uriHandler.getUriTreeDocument(rootDirUri) ?: throw SramLoadException("Cannot create root document: $rootDirUri")
        val romDocument = uriHandler.getUriDocument(rom.uri)

        val romFileName = romDocument?.name ?: throw SramLoadException("Cannot determine SRAM file name: ${romDocument?.uri}")
        val sramFileName = romFileName.replaceAfterLast('.', "sav", "$romFileName.sav").removeSuffix(".sav") + "$nameSuffix.sav"

        val sramDocument = rootDocument.findFile(sramFileName)
        return if (sramDocument != null) {
            sramDocument.uri
        } else {
            val newSramUri = rootDocument.createFile("application/*", sramFileName)?.uri
            if (newSramUri == null) {
                // It looks like some devices create the file just fine but return null. As a fallback, check if the SRAM file was actually created or not
                // Reference: https://www.ghisler.ch/board/viewtopic.php?p=370089#p370089
                rootDocument.findFile(sramFileName)?.uri ?: throw SramLoadException("Could not create temporary SRAM file at ${rootDocument.uri}")
            } else {
                newSramUri
            }
        }
    }
}