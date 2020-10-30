#!/bin/sh

NAME=$(basename $(pwd))
tar cvfz ../${NAME}.tar.gz ../${NAME}
