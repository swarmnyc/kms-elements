#!/usr/bin/env bash
oldName=compositemixer
newName=stylemixer
oldName1=COMPOSITEMIXER
newName1=STYLEMIXER
oldName2=composite_mixer
newName2=style_mixer
oldName3=CompositeMixer
newName3=StyleMixer


sed -e "s/$oldName/$newName/g" -e "s/$oldName1/$newName1/g" -e "s/$oldName2/$newName2/g" -e "s/$oldName3/$newName3/g" kms$oldName.c > kms$newName.c
sed -e "s/$oldName/$newName/g" -e "s/$oldName1/$newName1/g" -e "s/$oldName2/$newName2/g" -e "s/$oldName3/$newName3/g" kms$oldName.h > kms$newName.h

