import React from 'react'
import { RouteComponentProps } from '@reach/router'
import { Composition, Box } from 'atomic-layout'

import {
  Button,
  Statistic,
  Statistics,
  Slider,
  Switch,
} from '@electricui/components-desktop-blueprint'
import {
  Card,
  Divider,
  HTMLTable,
  Icon,
  Label,
  Text,
  Tab,
} from '@blueprintjs/core'
import {
  IntervalRequester,
  StateTree,
  useHardwareState,
} from '@electricui/components-core'

import {
  ChartContainer,
  LineChart,
  RealTimeDomain,
  TimeAxis,
  VerticalAxis,
} from '@electricui/components-desktop-charts'
import { MessageDataSource } from '@electricui/core-timeseries'

import { Printer } from '@electricui/components-desktop'

const FanMode = () => {
  const fanstate = useHardwareState(state => state.fan.state)

  if (fanstate == 0) {
    return <div>Off</div>
  } else if (fanstate == 1) {
    return <div>Stall</div>
  } else if (fanstate == 2) {
    return <div>Startup</div>
  } else if (fanstate == 3) {
    return <div>OK</div>
  }

  return <div>null</div>
}

const CoolingAreas = `
  TemperaturesArea
  FanArea
  `
const temperatureDataSource = new MessageDataSource('temp')

export const CoolingDetails = () => {
  const ambient_temp = useHardwareState(state => state.temp.ambient).toFixed(0)
  const regulator_temp = useHardwareState(
    state => state.temp.regulator,
  ).toFixed(0)
  const supply_temp = useHardwareState(state => state.temp.supply).toFixed(0)
  const fanspeed = useHardwareState(state => state.fan.rpm).toFixed(0)
  const fansetting = useHardwareState(state => state.fan.setpoint).toFixed(0)

  return (
    <div>
      <IntervalRequester variables={['temp']} interval={1000} />
      <ChartContainer>
        <LineChart
          dataSource={temperatureDataSource}
          accessor={state => state.temp.ambient}
          maxItems={1000}
        />
        <LineChart
          dataSource={temperatureDataSource}
          accessor={state => state.temp.regulator}
          maxItems={1000}
        />
        <LineChart
          dataSource={temperatureDataSource}
          accessor={state => state.temp.supply}
          maxItems={1000}
        />
        {/* Plot a 10-minute window */}
        <RealTimeDomain window={600000} delay={250} />
        <TimeAxis />
        <VerticalAxis />
      </ChartContainer>

      <Composition areas={CoolingAreas} gap={20}>
        {({ TemperaturesArea, FanArea }) => (
          <React.Fragment>
            <TemperaturesArea>
              <Statistics>
                <Statistic value={ambient_temp} label={`Ambient`} suffix="º" />
                <Statistic
                  value={regulator_temp}
                  label={`DC-DC Reg`}
                  suffix="º"
                />
                <Statistic value={supply_temp} label={`AC-DC PSU`} suffix="º" />
              </Statistics>
            </TemperaturesArea>
            <FanArea>
              <Statistics>
                <Statistic value={<FanMode />} label="operation" />
                <Statistic value={fanspeed} label={`RPM, at ${fansetting}%`} />
              </Statistics>
            </FanArea>
          </React.Fragment>
        )}
      </Composition>
      <br />
      <br />
      <Composition gap={20} templateCols="1fr 2fr">
        <Box>
          <Switch
            unchecked={{ fan_manual_en: 0 }}
            checked={{ fan_manual_en: 1 }}
          >
            Manual Fan Control
          </Switch>
        </Box>
        <Box>
          <Slider min={0} max={100} labelStepSize={25}>
            <Slider.Handle accessor="fan_man_speed" />
          </Slider>
        </Box>
      </Composition>
    </div>
  )
}