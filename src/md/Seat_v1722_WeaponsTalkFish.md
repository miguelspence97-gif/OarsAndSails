# v1.7.22 - weapons, talking and fishing while seated

1. Weapons (Crew::Stow / Unstow, Crew.inl): sitting down at the oars or the helm sheathes the player's weapon
   (DrawWeaponMagicHands(false), still equipped) and takes the shield off (ActorEquipManager::UnequipObject). Standing up
   puts the shield back on if still carried. Passengers (not the dog) do the same when they board; a weapon they draw again
   aboard out of combat is put away (checked 4 times a second); shields go back on in LetGo. Log "Crew: ... shield ... off".
2. No talking seated (CarrierActivateHooks, TESNPC vtable 4C/37): seated, no NPC shows [E] Talk; the player's activation
   of any NPC is refused while seated and for 1 s after sitting down or standing up (the E press that stood you up also
   reached the game and opened a passenger's dialogue). Log "Seat: talking to ... held off".
3. Fishing supplies from the seat: E with the crosshair on them stands you up (the fishing needs you on your feet and the
   Activate control, which is off while seated) and uses them 0.6 s later (Gear::UseTargetedSoon / TendUse). If the game
   uses them itself from the same press (ACTI Activate hook), the second use is dropped. Log "Gear: ... used from the
   seat", "Gear: ... activated by the player (ok|refused)".
