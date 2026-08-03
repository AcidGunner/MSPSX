# PSn00bSDK example CMake script
# (C) 2021 spicyjpeg - MPL licensed

cmake_minimum_required(VERSION 3.21)

project(
	MineSweeperPSX
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "Minesweeper Plus for PlayStation"
	HOMEPAGE_URL "http://acidps3.22web.org/"
)

psn00bsdk_add_executable(
	mspsx
	GPREL
	
	src/main.c
	src/pad.c
	src/pad.h
	src/spi.c
	src/spi.h
)

psn00bsdk_add_cd_image(
	iso
	mspsx
	iso.xml
	DEPENDS mspsx system.cnf
)