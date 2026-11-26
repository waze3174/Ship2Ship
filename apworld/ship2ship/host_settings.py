import os

import settings


class Ship2ShipSettings(settings.Group):
    """Ship2Ship generation settings"""

    class ComboRandoPath(settings.OptionalUserFilePath):
        """Path to comborando (comborando.exe on Windows), from a Ship2Ship install or the generator pack.
        It must sit next to the game libraries it ships with. Only generating needs it, not playing."""
        description = "Ship2Ship comborando"
        is_exe = True

        @classmethod
        def validate(cls, path: str) -> None:
            # An unset path resolves to Archipelago's own folder, which settings validates before we can say it is unset.
            if os.path.isdir(path):
                return
            super().validate(path)

    comborando_path: ComboRandoPath = ComboRandoPath("")
