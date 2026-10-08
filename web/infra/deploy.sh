#!/bin/zsh
# Deploys the web game to Azure App Service: infrastructure from main.bicep (previewed first), then the code.
#   web/infra/deploy.sh                       (az login first; RG, LOCATION and APP can be overridden)
set -e
cd "$(dirname "$0")/.."
RG=${RG:-rg-red-dead-dimension}
LOCATION=${LOCATION:-eastus}
APP=${APP:-red-dead-dimension}

az group create --name "$RG" --location "$LOCATION" --output none
az deployment group what-if --resource-group "$RG" --template-file infra/main.bicep --parameters appName="$APP"
az deployment group create --resource-group "$RG" --template-file infra/main.bicep --parameters appName="$APP" --output none

# Only what the server needs: its code, the page and assets, and its two dependencies.
STAGE=$(mktemp -d)
cp -R server.js game public "$STAGE"/
node -e "const p=require('./package.json'); delete p.devDependencies; p.scripts={start:'node server.js'}; require('fs').writeFileSync('$STAGE/package.json', JSON.stringify(p, null, 2))"
(cd "$STAGE" && npm install --omit=dev --silent --no-audit --no-fund && zip -qr ../rdd-web.zip .)
az webapp deploy --resource-group "$RG" --name "$APP" --src-path "$STAGE/../rdd-web.zip" --type zip --output none
echo "https://$APP.azurewebsites.net"
