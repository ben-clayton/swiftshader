EXT_PATH=~/.vscode/extensions/google.gdp-0.0.1
SRC_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

mkdir -p ${EXT_PATH}
cp ${SRC_PATH}/package.json ${EXT_PATH}
cp ${SRC_PATH}/icon.png ${EXT_PATH}
go build -o ${EXT_PATH}/std-socket-pipe ${SRC_PATH}/std-socket-pipe.go