# TODO:
#   make a backbone models for each target/subtarget?
#   update all models when we get state change
#   make the views
#   let the views render as needed
#

# loop that long polls
# call got_notification(role, subrole, state)
#
string_starts = (s, prefix) -> return s.slice(0, prefix.length) == prefix

class BeepModel extends Backbone.Model
    request: (method, params, on_success) ->
        beep_request(@get('context'), @get('object'), method, params,
                on_success)

class GroupModel extends BeepModel
    REQUIRED_OBJECTS: ['audio']

    defaults:
        ready: false

    initialize: ->
        @models =
            'app.webradio': new BeepModel
                context: @get('context')
                object: 'app.webradio'

    update: (object, new_state) ->
        if not @models[object]
            @models[object] = new BeepModel
                context: @get('context')
                object: object
        @models[object].set new_state
        @_check_ready()

    _check_ready: ->
        for obj in @REQUIRED_OBJECTS
            if not @models[obj]
                return
        console.log('ready!')
        @set ready: true

class GroupsCollection extends Backbone.Collection
    update: (key, event) ->
        group = @findWhere context: key
        if not group
            group = new GroupModel
                context: key
            @add group
        group.update(event.object, event.state)

    remove_missing: (remaining_keys) ->
        to_remove = []
        for group in @models
            if not _.contains(remaining_keys, group.get('context'))
                to_remove.push(group)
        for group in to_remove
            @remove(group)

window.groups = new GroupsCollection()

class GroupSelectorItemView extends Backbone.View
    initialize: ->
        @render()
        @app_selector_view = new AppSelectorView
            model: @model

    events:
        'click': 'select_group'

    render: ->
        @$el.html ich.tpl_group_selector_item @model.toJSON()

    select_group: ->
        console.log('group selected')
        app_controller.push_app_view @app_selector_view, @model.get('context')
        app_controller.mini_now_playing.set_model @model.models['audio']
        app_controller.players.set_model @model.models['audio']
        app_controller.history.set_model @model.models['audio']
        app_controller.now_playing.set_model @model.models['audio']

class GroupSelectorView extends Backbone.View
    initialize: ->
        @render()
        @listenTo(@collection, 'add', @render)
        @listenTo(@collection, 'remove', @render)
        @listenTo(@collection, 'change', @render)

    render: ->
        @$el.html ich.tpl_group_selector
        for group in @collection.where(ready: true)
            group_selector_item_view = new GroupSelectorItemView
                model: group
            @$('ul').append(group_selector_item_view.el)

class AppSelectorItemView extends Backbone.View
    initialize: ->
        @render()

    events:
        'click': 'select_app'

    render: ->
        @$el.html ich.tpl_app_selector_item(@options)

    select_app: ->
        app_controller.push_app_view @options.app_view, @options.app_name

class AppSelectorView extends Backbone.View
    initialize: ->
        @app_views =
            'webradio': new AppWebradioView
                model: @model.models['app.webradio']
        @render()

    render: ->
        @$el.html ich.tpl_app_selector()
        for app_name, app_view of @app_views
            app_selector_item_view = new AppSelectorItemView
                app_name: app_name
                app_view: app_view
            @$('ul').append(app_selector_item_view.el)

class AppWebradioView extends Backbone.View
    initialize: ->
        $.getJSON('https://s3-us-west-1.amazonaws.com/beepassets/stations.json',
            (data) =>
                @STATIONS = data
                console.log @STATIONS
                @render())

    events:
        'click li.station': 'choose_station'

    render: ->
        @$el.html ich.tpl_app_webradio
            stations: @STATIONS

    choose_station: (event) ->
        station_name = $(event.currentTarget).data('station-name')
        station_url = $(event.currentTarget).data('station-url')
        station_image_url = $(event.currentTarget).data('station-image-url')
        @model.request 'play_station',
            name: station_name
            url: station_url
            image_url: station_image_url
        app_controller.show_now_playing()

class NowPlayingView extends Backbone.View
    className: 'now-playing'

    set_model: (model) ->
        @model = model
        @off(null, @render)
        @listenTo(@model, 'change', @render)
        @render()

    render: ->
        context = @model.toJSON()
        app_name_fields = @model.get('station')?.app?.split('.')
        if app_name_fields
            context.app_name = app_name_fields[app_name_fields.length - 1]

        if context.track_info instanceof Array
            context.track_info = {}

        if @model.get('station')?.image_url and
                not @model.get('track_info')?.image_url
            context.track_info.image_url = context.station.image_url

        @$el.html ich.tpl_now_playing context

class NowPlayingMiniView extends Backbone.View
    set_model: (model) ->
        @model = model
        @off(null, @render)
        @listenTo(@model, 'change', @render)
        @render()

    events:
        'click #play_pause': 'play_pause'
        'click #skip': 'skip'

    render: ->
        #station = @model.get('station')
        #app_name = if station then parse_app_id(station['app']) else null
        playing = true
        if @model.get('audio_state') == 'paused'
            playing = false
        station = @model.get('station')
        if station
            station_name = station.name
            _app_name = station.app
            if _app_name
                fields =_app_name.split('.')
                if fields.length == 2
                    app_name = _app_name.split('.')[1]
            else
                app_name = _app_name

        @$el.html ich.tpl_now_playing_mini
            playing: playing
            station_name: station_name
            app_name: app_name

    play_pause: ->
        if @model.get('audio_state') == 'paused'
            @model.request 'smart_resume'
        else
            @model.request 'pause'

    skip: ->
        @model.request 'skip'
        app_controller.show_now_playing()

class PlayerView extends Backbone.View
    initialize: (options) ->
        @player_id = options.player_id
        @set_volume = _.throttle(@_set_volume, 100)
        @listenTo(@model, 'change', @on_model_change)
        @render()

    render: ->
        player = @model.get('players')[@player_id]
        if player
            @$el.html ich.tpl_player
                id: @player_id
                volume: player.volume

    events:
        'input.volume': 'volume_change'

    on_model_change: ->
        player = @model.get('players')[@player_id]
        if player
            @$('input.volume').val(player.volume)
            @$('span.volume').text(player.volume)

    volume_change: (event) ->
        target = $(event.target)
        volume = parseInt(target.val())
        @set_volume(volume)

    _set_volume: (vol) ->
        console.log('volume change ' + @player_id + ' '  + vol)
        player_volumes ={}
        player_volumes[@player_id] = vol
        @model.request 'set_volume',
            players: player_volumes


class PlayersView extends Backbone.View
    set_model: (model) ->
        @model = model
        @off(null, @render)
        @player_views = {}

        @listenTo(@model, 'change', @render)

        @set_volume = _.throttle(@_set_volume, 100)

        @$el.html ich.tpl_players()
        @render()

    events:
        'input input.master-volume': 'master_volume_change'

    master_volume_change: (event) ->
        target = $(event.target)
        volume = parseInt(target.val())
        @set_volume(volume)

    render: ->
        @$('input.master-volume').val(@model.get('master_volume'))
        @$('span.master-volume').text(@model.get('master_volume'))
        players = @model.get('players')
        have_change = false
        for id, player of players
            if not @player_views[id]
                have_change = true
                @player_views[id] = new PlayerView
                    player_id: id
                    model: @model
                @$('ul').append(@player_views[id].el)
        for id, player_view of @player_views
            if not players[id]
                have_change = true
        if have_change
            @$('ul').empty()
            # destroy the old views
            for id, player_view of @player_views
                if not players[id]
                    delete @player_views[id]
            # add new ones
            for id, player of players
                @player_views[id] = new PlayerView
                    player_id: id
                    model: @model
                @$('ul').append(@player_views[id].el)


    _set_volume: (vol) ->
        console.log('master volume change ' + @player_id + ' '  + vol)
        @model.request 'set_master_volume',
            volume: vol


class HistoryView extends Backbone.View
    set_model: (model) ->
        @model = model
        @off(null, @render)
        @listenTo(@model, 'change', @render)
        @render()

    events:
        'click a': 'history_item_click'

    render: ->
        history = @model.get('history')
        @$el.empty()
        @$el.append('<h3>Station History</h3>')
        for history_item in history
            appName = history_item['app']
            appName = appName.charAt(0).toUpperCase() + appName.slice(1)
            @$el.append ich.tpl_history_item
                app: appName
                name: history_item['name']
                index: history_item['index']

    history_item_click: (event) ->
        event.preventDefault()
        target = $(event.target)
        index = target.data('index')
        console.log('GOT INDEX: ' + index)
        @model.request 'play_history',
            index: index

class AppControllerView extends Backbone.View
    initialize: ->
        @app_views = []
        @nav_titles = []

        group_selector_view = new GroupSelectorView collection: groups
        @push_app_view(group_selector_view, 'Beep')
        @mini_now_playing = new NowPlayingMiniView
            el: @$("#now-playing-mini")
        @players = new PlayersView
            el: @$('#players')
        @history = new HistoryView
            el: @$('#history')
        @now_playing = new NowPlayingView()

    events:
        'click #back': 'pop_app_view'
        'click #now-playing-link': 'show_now_playing'

    push_app_view: (view, title) ->
        @app_views.push(view)
        @nav_titles.push(title)
        @show_top_app_view()

    pop_app_view: ->
        @app_views.pop()
        @nav_titles.pop()
        @show_top_app_view()

    show_top_app_view: ->
        # detach children so that their events are preserved, note if we
        # start creating views on the fly rather than creating them all up
        # front this will cause memory leaks, so it'd need to be changed.
        @$('#app').children().detach()
        @$('#app').html(_.last(@app_views).el)
        back = @nav_titles.length > 1

        is_now_playing = _.last(@nav_titles) == 'Now playing'
        @$('#nav').html ich.tpl_navbar
            title: _.last(@nav_titles)
            show_now_playing: !is_now_playing
            back: back

    show_now_playing: ->
        @push_app_view(@now_playing, 'Now playing')
        @$('#app').children().detach()
        @$('#app').html(@now_playing.el)

on_state_change = (key, events) ->
    if key == 'system'
        for event in events
            if event.event_type == 'master_removed'
                remaining_masters = _.keys(event.state.masters)
                remaining_masters = ('group.' + k for k in remaining_masters)
                groups.remove_missing(remaining_masters)
        # do nothing?
    else if string_starts(key, 'group.')
        for event in events
            groups.update(key, event)
    else if key != '_force_object_hack' # Quietly ignore this
        console.log('Unknown event key ' + key)

$ ->
    new StateSync on_state_change
    window.app_controller = new AppControllerView
        el: $('#main')
