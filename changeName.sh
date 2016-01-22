#!/usr/bin/env bash
oldName=stylecompositemixer
newName=stylemixer
oldName1=STYLECOMPOSITEMIXER
newName1=STYLEMIXER
oldName2=style_composite_mixer
newName2=style_mixer
oldName3=StyleCompositeMixer
newName3=StyleMixer


sed -e "s/$oldName/$newName/g" -e "s/$oldName1/$newName1/g" -e "s/$oldName2/$newName2/g" -e "s/$oldName3/$newName3/g" kms$oldName.c > kms$newName.c
sed -e "s/$oldName/$newName/g" -e "s/$oldName1/$newName1/g" -e "s/$oldName2/$newName2/g" -e "s/$oldName3/$newName3/g" kms$oldName.h > kms$newName.h

