import { Action, RunActionFunction } from '@electricui/core-actions'
import { Device, DeviceManager, Message } from '@electricui/core'

import fs from 'fs'

export const loadCollection = new Action(
  'load_collection',
  async (
    deviceManager: DeviceManager,
    runAction: RunActionFunction,
    filePath: string,
  ) => {
    return parseScene(filePath, runAction)
  },
)

export type ID = number

export type SceneAction = {
  id: ID
  action: string
  payload: any
  waitFor?: ID
  comment?: string
}

/**
 * Scene Groups are executed serially.
 *
 * The contents of a scene stream is a series of parallel execution pilelines
 */
export type SceneGroup = {
  [key: string]: Array<SceneAction>
}

/**
 * The base scene format
 */
export type SceneFormat = {
  metadata: {
    name: string
    formatVersion: string
    [key: string]: string
  }
  actions: Array<SceneGroup>
}

let inUse = false

async function parseScene(filePath: string, runAction: RunActionFunction) {
  if (inUse) {
    console.warn("I'm in use")
    return
  }

  try {
    inUse = true
    console.log('parsing scene', filePath)

    const sceneBytes = fs.readFileSync(filePath) // just read it all now
    const scene: SceneFormat = JSON.parse(sceneBytes.toString())

    if (scene.metadata.formatVersion !== '0.0.1') {
      throw new Error(
        `Parsing scene with incorrect version ${scene.metadata.formatVersion}`,
      )
    }

    const promiseMap = new Map<ID, Promise<any>>()

    for (const group of scene.actions) {
      const parallel: Array<Promise<any>> = []

      // Setup locks to wait until we've finished parsing before starting
      const seriesStartFunc: Array<() => void> = []

      for (const key of Object.keys(group)) {
        const actions = group[key]

        let series: Promise<any> = new Promise((resolve, reject) => {
          seriesStartFunc.push(resolve)
        })

        for (const actionStruct of actions) {
          const { id, action, payload, waitFor, comment } = actionStruct

          // Create our deferred run action
          const runThisAction = () => {
            const storedComment = comment
            if (storedComment) {
              console.log(storedComment)
            }
            return runAction(action, payload)
          }

          // If it has to be run at a specific time, attach it to its promise chain
          if (typeof waitFor !== 'undefined') {
            const promiseToWaitFor = promiseMap.get(waitFor)

            if (promiseToWaitFor) {
              const resultingPromise = promiseToWaitFor.then(() =>
                runThisAction(),
              )

              series = resultingPromise
            } else {
              console.warn(
                `Action ${id} could not find the promise to wait for, for action ${waitFor}`,
              )

              console.log('promise map at this time:', promiseMap)
            }
          } else {
            // execute the ones in series one after another
            series = series.then(() => runThisAction())
          }

          // series at this point is the promise that executes this action, so store it in case
          // any other actions rely on this one
          promiseMap.set(id, series)
        }

        parallel.push(series)
      }

      console.log('Executing group')

      // Start execution of the things in series
      seriesStartFunc.forEach(start => {
        start()
      })

      // Execute the parallel promises
      await Promise.all(parallel)
    }

    inUse = false
  } catch (e) {
    console.error(e)

    inUse = false
  }
}
