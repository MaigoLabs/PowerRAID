import { WebApiPsuBackend, type WebApiPsuBackendOptions } from './webApiBackend'
import { type PsuBackend } from './types'

export * from './protocol'
export * from './types'
export * from './webApiBackend'

export interface CreatePsuBackendOptions {
  webapi?: WebApiPsuBackendOptions
}

export function createPsuBackend(
  options: CreatePsuBackendOptions = {},
): PsuBackend {
  return new WebApiPsuBackend(options.webapi)
}
