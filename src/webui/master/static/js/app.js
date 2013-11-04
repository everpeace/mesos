(function() {
  'use strict';

  // d3 module
  angular.module('d3',[])
    .factory('d3',[function(){
      return d3;
    }]);

  angular.module('mesos', ['d3', 'ui.bootstrap'])
    .config(['$dialogProvider', '$routeProvider', function($dialogProvider, $routeProvider) {
      $routeProvider
        .when('/',
          {templateUrl: 'static/home.html', controller: 'HomeCtrl'})
        .when('/frameworks',
          {templateUrl: 'static/frameworks.html', controller: 'FrameworksCtrl'})
        .when('/frameworks/:id',
          {templateUrl: 'static/framework.html', controller: 'FrameworkCtrl'})
        .when('/slaves',
          {templateUrl: 'static/slaves.html', controller: 'SlavesCtrl'})
        .when('/slaves/:slave_id',
          {templateUrl: 'static/slave.html', controller: 'SlaveCtrl'})
        .when('/slaves/:slave_id/frameworks/:framework_id',
          {templateUrl: 'static/slave_framework.html', controller: 'SlaveFrameworkCtrl'})
        .when('/slaves/:slave_id/frameworks/:framework_id/executors/:executor_id',
          {templateUrl: 'static/slave_executor.html', controller: 'SlaveExecutorCtrl'})

        // Use a non-falsy template so the controller will still be executed.
        // Since the controller is intended only to redirect, the blank template
        // is fine.
        //
        // By design, controllers currently will not handle routes if the
        // template is falsy. There is an issue open in Angular to add that
        // feature:
        //
        //     https://github.com/angular/angular.js/issues/1838
        .when('/slaves/:slave_id/frameworks/:framework_id/executors/:executor_id/browse',
          {template: ' ', controller: 'SlaveExecutorRerouterCtrl'})
        .when('/slaves/:slave_id/browse',
          {templateUrl: 'static/browse.html', controller: 'BrowseCtrl'})
        .otherwise({redirectTo: '/'});

      $dialogProvider.options({dialogFade: true});

      ZeroClipboard.setDefaults({
        moviePath: '/static/obj/zeroclipboard-1.1.7.swf'
      });
    }])
    .filter('truncateMesosID', function() {
      return function(id) {
        if (id) {
          var truncatedIdParts = id.split('-');

          if (truncatedIdParts.length > 3) {
            return '…' + truncatedIdParts.splice(3, 3).join('-');
          } else {
            return id;
          }
        } else {
          return '';
        }
      };
    })
    .filter('truncateMesosState', function() {
      return function(state) {
        // Remove the "TASK_" prefix.
        return state.substring(5);
      };
    })
    .filter('isoDate', function($filter) {
      return function(date) {
        return $filter('date')(date, 'yyyy-MM-dd H:mm:ss');
      };
    })
    .filter('relativeDate', function() {
      return function(date) {
        return relativeDate(date);
      };
    })
    .filter('unixDate', function($filter) {
      return function(date) {
        if ((new Date(date)).getFullYear() == (new Date()).getFullYear()) {
          return $filter('date')(date, 'MMM dd HH:mm');
        } else {
          return $filter('date')(date, 'MMM dd YYYY');
        }
      };
    })
    .filter('dataSize', function() {
      return function(bytes) {
        if (bytes === null || bytes === undefined || isNaN(bytes)) {
          return '';
        } else if (bytes < 1024) {
          return bytes.toFixed() + ' B';
        } else if (bytes < (1024 * 1024)) {
          return (bytes / 1024).toFixed() + ' KB';
        } else if (bytes < (1024 * 1024 * 1024)) {
          return (bytes / (1024 * 1024)).toFixed() + ' MB';
        } else {
          return (bytes / (1024 * 1024 * 1024)).toFixed() + ' GB';
        }
      };
    })
    // Defines the 'clipboard' directive, which integrates copying to the user's
    // clipboard with an Adobe Flash object via the ZeroClipboard library.
    //
    // Text to be copied on click is specified with the 'data-clipboard-text'
    // attribute.
    //
    // The 'mouseenter' and 'mouseleave' events from the Flash object are exposed
    // to the directive's element via the 'clipboardhover' event. There is no
    // differentiation between enter/leave; they are both called 'clipboardhover'.
    //
    // Example:
    //
    //     <button class="btn btn-mini" clipboard
    //         data-clipboard-text="I'm in your clipboard!">
    //     </button>
    //
    // See: http://zeroclipboard.github.io/ZeroClipboard/
    .directive('clipboard', [function() {
      return {
        restrict: 'A',
        scope: true,
        template: '<i class="icon-file"></i>',

        link: function(scope, element, attrs) {
          var clip = new ZeroClipboard(element[0]);

          clip.on('mouseover', function() {
            angular.element(this).triggerHandler('clipboardhover');
          });

          clip.on('mouseout', function() {
            // TODO(ssorallen): Why is 'scope' incorrect here? It has to be
            // retrieved from the element explicitly to be correct.
            var elScope = angular.element(this).scope();

            // Restore tooltip content to its original value if it was changed by
            // this Clipboard instance.
            if (elScope && elScope.tt_content_orig) {
              elScope.tt_content = elScope.tt_content_orig;
              delete elScope.tt_content_orig;
            }

            angular.element(this).triggerHandler('clipboardhover');
          });

          clip.on('complete', function() {
            // TODO(ssorallen): Why is 'scope' incorrect here? It has to be
            // retrieved from the element explicitly to be correct.
            var elScope = angular.element(this).scope();

            if (elScope) {
              // Store the tooltip's original content so it can be restored when
              // the tooltip is hidden.
              elScope.tt_content_orig = elScope.tt_content;

              // Angular UI's Tooltip sets content on the element's scope in a
              // variable named 'tt_content'. The Tooltip has no public interface,
              // so set the value directly here to change the value of the tooltip
              // when content is successfully copied.
              elScope.tt_content = 'copied!';
              elScope.$apply();
            }
          });

          clip.on('load', function() {
            // The 'load' event fires only if the Flash file loads successfully.
            // The copy buttons will only display if the class 'flash' exists
            // on an ancestor.
            //
            // Browsers with no flash support will not append the 'flash' class
            // and will therefore not see the copy buttons.
            angular.element('html').addClass('flash');
          });
        }
      };
    }])
	// Define d3-bar directive.
	// This directive renders accumulated bar chart.
	// e.g.
	// <d3-bar data="<key for data in scope>"
	//   barValue="<key for bar>", barColor="<key for bar color>"
	//   label="<key for data label>", labelColor="<key for label color>"></d3-bar>
    .directive('d3Bar', ['d3', function(d3) {
      return {
        restrict: 'EA',
        scope: {
          data: "=",
          label: "@",
          labelColor: "@",
          barValue: "@",
          barColor: "@",
          onClick: "&"
        },
        link: function(scope, iElement, iAttrs) {
          var svg = d3.select(iElement[0])
              .append("svg")
              .attr("width", "100%");

          // on window resize, re-render d3 canvas
          window.onresize = function() {
            return scope.$apply();
          };
          scope.$watch(function(){
              return angular.element(window)[0].innerWidth;
            }, function(){
              return scope.render(scope.data);
            }
          );

          // watch for data changes and re-render
          scope.$watch('data', function(newVals, oldVals) {
            return scope.render(newVals);
          }, true);

          // define render function
          scope.render = function(data){
            // remove all previous items before render
            svg.selectAll("*").remove();

            // setup variables
            var elm_width = d3.select(iElement[0])[0][0].offsetWidth
            var width, height, max;
            var margin = {top:20, left:elm_width*0.025, right:elm_width*0.025};
            var bar_height = 20, bar_y_padding=bar_height*0.1;
            //svg width and height
            width = elm_width - (margin.left + margin.right);
            height = margin.top + bar_y_padding + bar_height;

            // set the height based on the calculations above
            var sum = _.reduce(data, function(memo, d){ return memo + d[scope.barValue]; }, 0); 
            if(sum > 0){
              svg.attr('height', height);
            } else {
              svg.attr('height', 0);
            }

            // converting data for stacked bar graph(calcuating x0)
            var _converted = [];
            _.reduce(data,function(memo, d){
              var _d = {};
              _d[scope.label] = d[scope.label];
              _d[scope.labelColor] = d[scope.labelColor];
              _d[scope.barValue] = d[scope.barValue];
              _d[scope.barColor] = d[scope.barColor];
              _d.x0 = memo;
              _converted.push(_d);
              return memo + d[scope.barValue];
            },0)

            var xScale = d3.scale.linear()
                          .domain([0,sum]).range([margin.left, margin.left+width]);
            var yScale = d3.scale.linear()
                          .domain([0,height]).range([margin.top, height + margin.top]);
            var numTicks = sum <= 1 ? 1
                           :sum < 10 ? 2
                           : 10;
            var xAxis = d3.svg.axis()
                          .scale(xScale)
                          .ticks(numTicks)
                          .orient("top");

            // TODO(everpeace) support Axis formatter
            svg.append('g')
              .attr({
                class: "axis",
                transform: "translate(0, +"+ yScale(0) +")"
              })
              .call(xAxis);

            //create the rectangles for the bar chart
            svg.selectAll("rect")
              .data(_converted)
              .enter()
                .append("rect")
                .attr("fill", function(d){ return d[scope.barColor]; })
                .attr("height", bar_height) // height of each bar
                .attr("width", function(d){
                    return xScale(d[scope.barValue]) - margin.left;
                })
                .attr("x", function(d){
                  return xScale(d.x0);
                })
                .attr("y", function(d){
                  return yScale(bar_y_padding);
                })
                // TODO(everpeace) support to tooltip
                .on("click", function(d, i){
                    return alert(""+d[scope.label]+":"+d[scope.barValue]);
                    // return scope.onClick({
                    //   data:d,
                    //   barValue:scope.barValue,
                    //   barColor:scope.barColor,
                    //   label:scope.label,
                    //   labelColor:scope.labelColor
                    // });
                })

            // svg.selectAll("text")
            //   .data(_converted)
            //   .enter()
            //     .append("text")
            //     .attr("fill", function(d){
            //       return d[scope.labelColor];
            //     })
            //     .attr("x", function(d){
            //       return xScale(d.x0)+5;
            //     })
            //     .attr("y", function(d){
            //       return yScale(bar_y_padding + bar_height * 0.8);
            //     })
            //     .text(function(d){
            //       return d[scope.label];
            //     });
          };
        }
      };
    }]);
})();
