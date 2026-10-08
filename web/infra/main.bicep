// Red Dead Dimension on the web: one Linux App Service running the Node game server. The page, the 3D assets and the
// duel's WebSockets all come from the same process on one HTTPS address.
//
// Choices:
// - App Service, not Static Web Apps or Functions: the duel needs long-lived WebSockets and a game loop in memory.
// - B1: the smallest tier with Always On (no cold start mid-invite) and enough WebSocket connections (Free allows 5).
// - One instance on purpose: rooms live in the server's memory. Scaling out would need a shared store first.
// Security: HTTPS only, TLS 1.2+, FTP off. No secrets: the game has no accounts and no keys.

@description('Globally unique app name. The game is served at https://<appName>.azurewebsites.net')
param appName string = 'red-dead-dimension'

@description('Region for the plan and the app.')
param location string = resourceGroup().location

@description('App Service plan size.')
param sku string = 'B1'

resource plan 'Microsoft.Web/serverfarms@2024-04-01' = {
  name: '${appName}-plan'
  location: location
  kind: 'linux'
  sku: {
    name: sku
    capacity: 1
  }
  properties: {
    reserved: true // Linux
  }
}

resource app 'Microsoft.Web/sites@2024-04-01' = {
  name: appName
  location: location
  kind: 'app,linux'
  properties: {
    serverFarmId: plan.id
    httpsOnly: true
    clientAffinityEnabled: false
    siteConfig: {
      linuxFxVersion: 'NODE|22-lts'
      appCommandLine: 'node server.js'
      webSocketsEnabled: true
      alwaysOn: true
      minTlsVersion: '1.2'
      ftpsState: 'Disabled'
      healthCheckPath: '/healthz'
      appSettings: [
        {
          name: 'NODE_ENV'
          value: 'production'
        }
        {
          // The deploy script ships node_modules already installed (express and ws only).
          name: 'SCM_DO_BUILD_DURING_DEPLOYMENT'
          value: 'false'
        }
      ]
    }
  }
}

output url string = 'https://${app.properties.defaultHostName}'
